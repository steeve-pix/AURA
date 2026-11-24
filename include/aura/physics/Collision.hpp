#pragma once
#include "CollisionShape.hpp"
#include "RigidBody3D.hpp"
#include <vector>

namespace aura::physics {
    // Dry, grippy ground: static friction holds until the support limit is
    // exceeded; sliding uses the lower dynamic coefficient. Material assumption,
    // not a measured property of AURA. Both impulses remain load-limited.
    inline constexpr float groundStaticFriction = 0.8f;
    inline constexpr float groundDynamicFriction = 0.6f;

    enum class FloorFrictionModel { Coulomb, LegacyVelocityDamping };

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
        std::vector<math::Vec3> frictionImpulses; // Tangential impulses summed per contact.
        math::Vec3 dampingDeltaVelocity{};
        math::Vec3 dampingDeltaOmega{};
        math::Vec3 normalDeltaVelocity{};
        math::Vec3 normalDeltaOmega{};
        math::Vec3 frictionDeltaVelocity{};
        math::Vec3 frictionDeltaOmega{};
        double energyBefore = 0.0, energyAfterDamping = 0.0, energyAfterNormal = 0.0, energyAfterFriction = 0.0;
        float linearSpeedBefore = 0.0f, linearSpeedAfterDamping = 0.0f, linearSpeedAfterNormal = 0.0f,
              linearSpeedAfterFriction = 0.0f;
        float angularSpeedBefore = 0.0f, angularSpeedAfterDamping = 0.0f, angularSpeedAfterNormal = 0.0f,
              angularSpeedAfterFriction = 0.0f;
        std::vector<FloorNormalImpulseAudit> impulseEvents;
    };

    void resolveFloorCollision(RigidBody3D &body, const math::Vec3 &size, float floorY, float dt,
                               FloorCollisionAudit *audit = nullptr,
                               FloorFrictionModel friction = FloorFrictionModel::LegacyVelocityDamping,
                               bool applyAngularDamping = true);

    void resolveFloorCollision(RigidBody3D &body, const math::Vec3 &size, CollisionShape shape,
                               float floorY, float dt, FloorCollisionAudit *audit = nullptr,
                               FloorFrictionModel friction = FloorFrictionModel::LegacyVelocityDamping,
                               bool applyAngularDamping = true);
}
