#pragma once

#include <cmath>

#include "Vec2.hpp"

namespace aura::math {
    constexpr float DEFAULT_PRECISION = 1.0e-5f;

    inline bool nearlyEqual(float a, float b, float epsilon = DEFAULT_PRECISION) noexcept {
        return std::fabs(a - b) <= epsilon;
    }

    inline float cross(const Vec2 &a, const Vec2 &b) noexcept {
        return a.x * b.y - a.y * b.x;
    }
}
