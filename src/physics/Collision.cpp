#include "aura/physics/Collision.hpp"

namespace aura::physics {
    bool intersectFloor(const RigidBody3D &body, float halfHeight, float floorY) {
        return body.position.y - halfHeight < floorY;
    }

    void resolveFloorCollision(RigidBody3D &body, float halfHeight, float floorY) {
        const float bottom =
                body.position.y - halfHeight;

        if (bottom >= floorY) {
            return;
        }

        const float penetration =
                floorY - bottom;

        body.position.y += penetration;

        if (body.velocity.y < 0.0f) {
            body.velocity.y = 0.0f;
        }
    }
}
