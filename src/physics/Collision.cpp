#include "aura/physics/Collision.hpp"

#include <algorithm>

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

        const float penetration =
                floorY - bottom;

        body.position.y += penetration;

        const float frictionStrength = 6.0f;

        const float damping =
                std::max(0.0f, 1 - frictionStrength * dt);


        body.velocity.x *= damping;
        body.velocity.z *= damping;

        if (body.velocity.y < 0.0f) {
            constexpr float restitution = 0.5f;
            body.velocity.y *= -restitution;
        }
    }
}
