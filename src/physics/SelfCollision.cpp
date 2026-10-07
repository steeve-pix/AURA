#include "aura/physics/SelfCollision.hpp"
#include <algorithm>
#include <array>

namespace aura::physics {
    namespace {
        template<class Body> auto partsOf(Body &body) {
            return std::array{&body.head, &body.neck, &body.torso, &body.pelvis,
                &body.leftUpperArm, &body.leftForearm, &body.leftHand,
                &body.rightUpperArm, &body.rightForearm, &body.rightHand,
                &body.leftThigh, &body.leftShin, &body.leftFoot,
                &body.rightThigh, &body.rightShin, &body.rightFoot};
        }
    }

    CollisionSphere collisionSphere(const body::BodyPart3D &part) {
        return {part.body.position, 0.5f * std::min({part.size.x, part.size.y, part.size.z})};
    }

    bool shouldSelfCollide(const body::BodyPart3D &a, const body::BodyPart3D &b,
                           const body::AuraBody3D &owner, const body::AuraSkeleton3D &skeleton) {
        return &a != &b && !skeleton.areDirectlyConnected(owner, a, b);
    }

    void resolveSelfCollision(body::BodyPart3D &a, body::BodyPart3D &b) {
        if (&a == &b) return;
        const auto sphereA = collisionSphere(a), sphereB = collisionSphere(b);
        const auto delta = sphereB.center - sphereA.center;
        const float distance = delta.length();
        const float penetration = sphereA.radius + sphereB.radius - distance;
        if (penetration <= 0.0f) return;
        const auto normal = distance > 0.0f ? delta * (1.0f / distance) : math::Vec3{1, 0, 0};
        const float inverseA = 1.0f / a.body.mass, inverseB = 1.0f / b.body.mass;
        const float total = inverseA + inverseB;
        a.body.position -= normal * (penetration * inverseA / total);
        b.body.position += normal * (penetration * inverseB / total);
    }

    void resolveBodySelfCollisions(body::AuraBody3D &body, const body::AuraSkeleton3D &skeleton) {
        const auto parts = partsOf(body);
        for (std::size_t i = 0; i < parts.size(); ++i)
            for (std::size_t j = i + 1; j < parts.size(); ++j)
                if (shouldSelfCollide(*parts[i], *parts[j], body, skeleton))
                    resolveSelfCollision(*parts[i], *parts[j]);
    }

    bool componentTranslationIsSelfCollisionSafe(
        const body::AuraBody3D &owner, const body::AuraSkeleton3D &skeleton,
        const std::vector<body::BodyPart3D *> &component,
        const math::Vec3 &delta, float tolerance) {
        const auto parts = partsOf(owner);
        for (const auto *a : component) for (const auto *b : parts) {
            if (std::find(component.begin(), component.end(), b) != component.end()) continue;
            if (!shouldSelfCollide(*a, *b, owner, skeleton)) continue;
            const auto sphereA = collisionSphere(*a), sphereB = collisionSphere(*b);
            const float before = sphereA.radius + sphereB.radius - (sphereB.center-sphereA.center).length();
            const float after = sphereA.radius + sphereB.radius - (sphereB.center-sphereA.center-delta).length();
            if (after > std::max(0.0f, before) + tolerance) return false;
        }
        return true;
    }

    float maximumSelfCollisionPenetration(const body::AuraBody3D &body,
                                          const body::AuraSkeleton3D &skeleton) {
        const auto parts = partsOf(body);
        float maximum = 0.0f;
        for (std::size_t i = 0; i < parts.size(); ++i)
            for (std::size_t j = i + 1; j < parts.size(); ++j) {
                if (!shouldSelfCollide(*parts[i], *parts[j], body, skeleton)) continue;
                const auto a = collisionSphere(*parts[i]), b = collisionSphere(*parts[j]);
                maximum = std::max(maximum, a.radius + b.radius - (b.center - a.center).length());
            }
        return maximum;
    }
}
