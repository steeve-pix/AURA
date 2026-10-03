#pragma once
#include "aura/math/Quaternion.hpp"
#include "aura/math/Vec3.hpp"

namespace aura:: physics {
    struct RigidBody3D {
        math::Vec3 position{};
        math::Vec3 velocity{};
        math::Vec3 acceleration{};
        math::Vec3 force{};

        math::Quaternion orientation{};
        math::Vec3 angularVelocity{};
        math::Vec3 angularAcceleration{};
        math::Vec3 torque{};

        math::Vec3 momentOfInertia{1.0f, 1.0f, 1.0f};

        float mass = 1.0f;
        float restitution = 0.5f;
    };
}
