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

        const float damping =
                std::max(0.0f, 1 - frictionStrength * dt);


        body.velocity.x *= damping;
        body.velocity.z *= damping;

        const math::Vec3 normal{0.0f, 1.0f, 0.0f};
        const float normalVelocity = body.velocity.dot(normal);

        if (normalVelocity < 0.0f) {
            const float impulseMagnitude =
                    -(1.0f + body.restitution) * normalVelocity * body.mass;
            const math::Vec3 impulse = normal * impulseMagnitude;

            applyImpulseAtPoint(body, impulse, contactPoint);
        }
    }
}
