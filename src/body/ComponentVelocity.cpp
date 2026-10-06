#include "aura/body/ComponentVelocity.hpp"
#include "aura/body/ComponentMass.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include <algorithm>
#include <stdexcept>

namespace aura::body {
    math::Vec3 componentAnchorVelocity(std::span<BodyPart3D *const> component,
                                      const math::Vec3 &anchor, const math::Vec3 &direction) {
        const auto center = componentCenterOfMass(component);
        const auto r = anchor - center;
        const auto cross = r.cross(direction.normalized());
        math::Vec3 omega{};
        if (cross.lengthSquared() > 1e-12f) {
            const auto axis = cross.normalized();
            const float inertia = componentMomentOfInertiaAboutAxis(component, center, axis);
            omega = axis * (componentAngularMomentum(component).dot(axis) / inertia);
        }
        return componentCenterOfMassVelocity(component) + omega.cross(r);
    }

    ComponentVelocityAudit correctJointComponentModeVelocity(
        BodyPart3D &parent, BodyPart3D &child, const Joint3D &joint,
        std::span<BodyPart3D *const> component) {
        if (std::find(component.begin(), component.end(), &parent) != component.end() ||
            std::find(component.begin(), component.end(), &child) == component.end())
            throw std::invalid_argument("Component must contain child and exclude parent");
        ComponentVelocityAudit audit;
        const float mass = componentMass(component);
        const auto center = componentCenterOfMass(component);
        const auto anchorA = localToWorldPoint(parent, joint.localAnchorA);
        const auto anchorB = localToWorldPoint(child, joint.localAnchorB);
        const auto actualRelative = physics::velocityAtWorldPoint(child.body, anchorB) -
                                    physics::velocityAtWorldPoint(parent.body, anchorA);
        if (actualRelative.lengthSquared() < 1e-6f) return audit;
        audit.direction = actualRelative.normalized();
        const auto rA = anchorA - parent.body.position;
        const auto rB = anchorB - center;
        const auto crossB = rB.cross(audit.direction);
        float inertiaB = 0.0f, rotationalB = 0.0f;
        if (crossB.lengthSquared() > 1e-12f) {
            inertiaB = componentMomentOfInertiaAboutAxis(component, center, crossB);
            rotationalB = crossB.lengthSquared() / inertiaB;
        }
        const auto inverseParentInertia = [&](const math::Vec3 &worldVector) {
            const auto local = parent.body.orientation.conjugate().rotate(worldVector);
            const auto &I = parent.body.momentOfInertia;
            return parent.body.orientation.rotate({local.x / I.x, local.y / I.y, local.z / I.z});
        };
        const auto relativeMode = componentAnchorVelocity(component, anchorB, audit.direction) -
                                  physics::velocityAtWorldPoint(parent.body, anchorA);
        audit.modeSpeedBefore = relativeMode.dot(audit.direction);
        const auto crossA = rA.cross(audit.direction);
        const float denominator = 1.0f / parent.body.mass + 1.0f / mass +
                                  crossA.dot(inverseParentInertia(crossA)) + rotationalB;
        const auto impulse = audit.direction * (-audit.modeSpeedBefore / denominator);
        audit.predictedWork = relativeMode.dot(impulse);
        const auto apply = [&](physics::RigidBody3D &b, const math::Vec3 &dv, const math::Vec3 &dw) {
            const auto localOld = b.orientation.conjugate().rotate(b.angularVelocity);
            const auto localDelta = b.orientation.conjugate().rotate(dw);
            const auto &I = b.momentOfInertia;
            audit.linearWork += b.mass * b.velocity.dot(dv);
            audit.angularWork += I.x * localOld.x * localDelta.x +
                                 I.y * localOld.y * localDelta.y + I.z * localOld.z * localDelta.z;
            audit.quadraticEnergy += 0.5 * b.mass * dv.lengthSquared() +
                0.5 * (I.x * localDelta.x * localDelta.x + I.y * localDelta.y * localDelta.y + I.z * localDelta.z * localDelta.z);
            b.velocity += dv;
            b.angularVelocity += dw;
        };
        apply(parent.body, -impulse * (1.0f / parent.body.mass), inverseParentInertia(rA.cross(-impulse)));
        const auto deltaV = impulse * (1.0f / mass);
        const auto deltaW = inertiaB > 0.0f ? rB.cross(impulse) * (1.0f / inertiaB) : math::Vec3{};
        for (auto *part : component) apply(part->body, deltaV + deltaW.cross(part->body.position - center), deltaW);
        audit.modeSpeedAfter = (componentAnchorVelocity(component, anchorB, audit.direction) -
            physics::velocityAtWorldPoint(parent.body, anchorA)).dot(audit.direction);
        return audit;
    }
}
