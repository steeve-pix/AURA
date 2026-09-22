#pragma once

#include <cmath>
#include <numbers>

#include "Vec2.hpp"

namespace aura::math {
    constexpr float DEFAULT_PRECISION = 1.0e-5f;

    inline bool nearlyEqual(float a, float b, float epsilon = DEFAULT_PRECISION) noexcept {
        return std::fabs(a - b) <= epsilon;
    }

    inline float cross(const Vec2 &a, const Vec2 &b) noexcept {
        return a.x * b.y - a.y * b.x;
    }

    inline float normalizeAngle(float angle) noexcept {
        constexpr float pi =
                std::numbers::pi_v<float>;

        constexpr float twoPi =
                2.0f * pi;

        while (angle > pi) {
            angle -= twoPi;
        }

        while (angle < -pi) {
            angle += twoPi;
        }

        return angle;
    }
}
