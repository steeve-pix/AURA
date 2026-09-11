#pragma once

#include <cmath>

#include "Vec2.hpp"

namespace aura::math {
    inline Vec2 rotate(const Vec2 &vec, float angle) noexcept {
        const float cos = std::cos(angle);
        const float sin = std::sin(angle);

        return Vec2{
            vec.x * cos - vec.y * sin,
            vec.x * sin + vec.y * cos
        };
    }
}
