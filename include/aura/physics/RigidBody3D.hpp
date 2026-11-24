#pragma once
#include "aura/math/Quaternion.hpp"
#include "aura/math/Vec3.hpp"

namespace aura:: physics {
    struct RigidBody3D {
        math::Vec3 position{}; // 0.0 0.0 0.0
        math::Vec3 velocity{}; // 0.0 0.0 0.0
        math::Vec3 acceleration{}; // 0.0 0.0 0.0
        math::Vec3 force{}; // 0.0 0.0 0.0

        math::Quaternion orientation{}; // 0.0 0.0 0.0 0.0
        math::Vec3 angularVelocity{}; // 0.0 0.0 0.0
        math::Vec3 angularAcceleration{}; // 0.0 0.0 0.0
        math::Vec3 torque{}; // 0.0 0.0 0.0

        math::Vec3 momentOfInertia{1.0f, 1.0f, 1.0f};

        float mass = 1.0f;
        float restitution = 0.0f;
    };
}
