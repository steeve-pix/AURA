#pragma once
#include "RigidBody3D.hpp"
#include <vector>

namespace aura::physics {
    bool intersectFloor(const RigidBody3D &body, float halfHeight, float floorY);

    // Optional measurements only; enabling them does not change the response.
    struct FloorCollisionAudit {
        float lowestBefore = 0.0f;
        float lowestAfterPosition = 0.0f;
        std::vector<math::Vec3> contacts;
        std::vector<math::Vec3> normalImpulses; // Sum per contact across four passes.
        math::Vec3 dampingDeltaVelocity{};
        math::Vec3 dampingDeltaOmega{};
        math::Vec3 normalDeltaVelocity{};
        math::Vec3 normalDeltaOmega{};
    };
    void resolveFloorCollision(RigidBody3D &body, const math::Vec3 &size, float floorY, float dt,
                               FloorCollisionAudit *audit = nullptr);
}
