#pragma once

#include <cmath>

namespace aura::math {
    constexpr float DEFAULT_PRECISION = 1.0e-5f;

    inline bool nearlyEqual(float a, float b, float epsilon = DEFAULT_PRECISION) noexcept {
        return std::fabs(a - b) <= epsilon;
    }
}
