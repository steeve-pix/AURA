#pragma once
#include <numbers>

#include "aura/math/Vec2.hpp"

namespace aura::physics {
    struct Joint2D {
        math::Vec2 localAnchorA{};
        math::Vec2 localAnchorB{};

        float minAngle = -1.0f * std::numbers::pi_v<float>;
        float maxAngle = std::numbers::pi_v<float>;

        float targetAngle = 0.0f;
        float motorStiffness = 10.0f;
        float motorDamping = 2.0f;
    };
}
