#include "aura/physics/Collision.hpp"
#include "aura/physics/Impulse.hpp"

#include <algorithm>

#include "aura/physics/BodyGeometry.hpp"

namespace aura::physics {
    bool intersectFloor(const RigidBody3D &body, float halfHeight, float floorY) {
        return body.position.y - halfHeight < floorY;
    }

    void resolveFloorCollision(RigidBody3D &body, float halfHeight, float floorY, float dt) {
        const float bottom =
                body.position.y - halfHeight;

        if (bottom >= floorY) {
            return;
        }

        const auto contactPoint =
                lowestPoint(body, {1.0f, 1.0f, 1.0f});

        const float penetration =
                floorY - contactPoint.y;

        body.position.y += penetration;

        constexpr float frictionStrength = 6.0f;
        constexpr float angularDamping = 1.5f;


        const float damping =
                std::max(0.0f, 1 - frictionStrength * dt);

        const float aDamping =
                std::max(0.0f, 1.0f - angularDamping * dt);

        body.velocity.x *= damping;
        body.velocity.z *= damping;

        body.angularVelocity *= aDamping;

        const math::Vec3 r =
                contactPoint - body.position;

        const math::Vec3 normal{0.0f, 1.0f, 0.0f};

        const math::Vec3 rCrossN =
                r.cross(normal);

        const math::Vec3 inverseInertiaTimesRCrossN{
            rCrossN.x / body.momentOfInertia.x,
            rCrossN.y / body.momentOfInertia.y,
            rCrossN.z / body.momentOfInertia.z,
        };

        const float rotationalTerm =
                inverseInertiaTimesRCrossN.cross(r).dot(normal);

        const float effectiveInverseMass =
                (1.0f / body.mass) + rotationalTerm;

        const auto contactVelocity =
                velocityAtWorldPoint(body, contactPoint);

        const float normalVelocity = contactVelocity.dot(normal);

        if (normalVelocity < 0.0f) {
            const float impulseMagnitude =
                    -(1.0f + body.restitution) * normalVelocity / effectiveInverseMass;
            const math::Vec3 impulse = normal * impulseMagnitude;

            applyImpulseAtPoint(body, impulse, contactPoint);
        }
    }
}
