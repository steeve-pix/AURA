#pragma once
#include "RigidBody3D.hpp"
#include <vector>

namespace aura::physics {
    struct FloorNormalImpulseAudit {
        int iteration = 0;
        std::size_t contact = 0;
        float magnitude = 0.0f;
        float normalVelocityBefore = 0.0f, normalVelocityAfter = 0.0f;
        double energyBefore = 0.0, energyAfter = 0.0;
        RigidBody3D before{}, after{};
    };

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
        double energyBefore = 0.0, energyAfterDamping = 0.0, energyAfterNormal = 0.0;
        float linearSpeedBefore = 0.0f, linearSpeedAfterDamping = 0.0f, linearSpeedAfterNormal = 0.0f;
        float angularSpeedBefore = 0.0f, angularSpeedAfterDamping = 0.0f, angularSpeedAfterNormal = 0.0f;
        std::vector<FloorNormalImpulseAudit> impulseEvents;
    };
    void resolveFloorCollision(RigidBody3D &body, const math::Vec3 &size, float floorY, float dt,
                               FloorCollisionAudit *audit = nullptr);
}
