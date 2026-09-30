#pragma once
#include "aura/math/Quaternion.hpp"
#include "aura/math/Vec3.hpp"

namespace aura:: physics {
    struct RigidBody3D {
        math::Vec3 position{};
        math::Vec3 velocity{};

        math::Quaternion orientation{};
        math::Vec3 angularVelocity{};

        float mass = 1.0f;
    };
}
