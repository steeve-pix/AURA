#pragma once

namespace aura::math {
    struct Vec2 {
        float x{0.0f}, y{0.0f};

        constexpr Vec2 &operator+=(const Vec2 &rhs) noexcept {
            x += rhs.x, y += rhs.y;
            return *this;
        };

        constexpr Vec2 &operator-=(const Vec2 &rhs) noexcept {
            x -= rhs.x, y -= rhs.y;
            return *this;
        }

        constexpr Vec2 operator+(const Vec2 &rhs) const noexcept {
            Vec2 result = *this;
            result += rhs;
            return result;
        }

        constexpr Vec2 operator-(const Vec2 &rhs) const noexcept {
            Vec2 result = *this;
            result -= rhs;
            return result;
        }
    };
}
