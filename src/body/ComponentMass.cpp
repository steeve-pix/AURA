#include "aura/body/ComponentMass.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace aura::body {
    float componentMass(std::span<BodyPart3D *const> parts) {
        float total = 0.0f;
        for (const auto *part : parts) {
            if (!part || !std::isfinite(part->body.mass) || part->body.mass <= 0.0f)
                throw std::invalid_argument("Component parts require finite positive masses");
            total += part->body.mass;
        }
        if (!std::isfinite(total)) throw std::invalid_argument("Component mass overflow");
        return total;
    }

    math::Vec3 componentCenterOfMass(std::span<BodyPart3D *const> parts) {
        const float total = componentMass(parts);
        if (total == 0.0f) throw std::invalid_argument("Empty component has no center of mass");
        math::Vec3 weightedPosition{};
        for (const auto *part : parts) weightedPosition += part->body.position * part->body.mass;
        return weightedPosition * (1.0f / total);
    }

    math::Vec3 componentCenterOfMassVelocity(std::span<BodyPart3D *const> parts) {
        const float total = componentMass(parts);
        if (total == 0.0f) throw std::invalid_argument("Empty component has no COM velocity");
        math::Vec3 momentum{};
        for (const auto *part : parts) momentum += part->body.velocity * part->body.mass;
        return momentum * (1.0f / total);
    }

    math::Vec3 componentAngularMomentum(std::span<BodyPart3D *const> parts) {
        const auto center = componentCenterOfMass(parts);
        const auto velocity = componentCenterOfMassVelocity(parts);
        math::Vec3 momentum{};
        for (const auto *part : parts) {
            const auto &b = part->body;
            const auto localOmega = b.orientation.conjugate().rotate(b.angularVelocity);
            const auto spin = b.orientation.rotate({
                b.momentOfInertia.x * localOmega.x,
                b.momentOfInertia.y * localOmega.y,
                b.momentOfInertia.z * localOmega.z});
            momentum += spin + (b.position - center).cross((b.velocity - velocity) * b.mass);
        }
        return momentum;
    }

    float componentMomentOfInertiaAboutAxis(
        std::span<BodyPart3D *const> parts,
        const math::Vec3 &pivot,
        const math::Vec3 &worldAxis) {
        componentMass(parts); // Validate dynamic masses consistently.
        const float axisLength = worldAxis.length();
        if (!std::isfinite(axisLength) || axisLength <= 0.0f)
            throw std::invalid_argument("Component inertia requires a finite nonzero axis");
        const auto axis = worldAxis * (1.0f / axisLength);
        double total = 0.0;
        for (const auto *part : parts) {
            const auto localAxis = part->body.orientation.conjugate().rotate(axis).normalized();
            const auto &inertia = part->body.momentOfInertia;
            if (!std::isfinite(inertia.x) || !std::isfinite(inertia.y) || !std::isfinite(inertia.z) ||
                inertia.x <= 0.0f || inertia.y <= 0.0f || inertia.z <= 0.0f)
                throw std::invalid_argument("Component inertia requires finite positive local inertia");
            const auto r = part->body.position - pivot;
            // |r cross axis| squared avoids cancellation in |r|^2 - (r dot axis)^2.
            const float perpendicularSquared = r.cross(axis).lengthSquared();
            total += inertia.x * localAxis.x * localAxis.x +
                     inertia.y * localAxis.y * localAxis.y +
                     inertia.z * localAxis.z * localAxis.z + part->body.mass * perpendicularSquared;
        }
        if (!std::isfinite(total) || total > std::numeric_limits<float>::max())
            throw std::invalid_argument("Component inertia overflow or nonfinite geometry");
        return static_cast<float>(total);
    }
}
