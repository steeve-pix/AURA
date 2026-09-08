#pragma once

#include <cmath>

#include "aura/physics/Body2D.hpp"
#include "aura/physics/Body2D.hpp"
#include "aura/physics/Body2D.hpp"

namespace aura::math {
    struct Vec2 {
        float x{0.0f}, y{0.0f};

        constexpr Vec2 &operator+=(const Vec2 &rhs) noexcept {
            x += rhs.x;
            y += rhs.y;
            return *this;
        }

        constexpr Vec2 operator/=(float scalar) noexcept {
            x /= scalar;
            y /= scalar;
            return *this;
        }

        constexpr Vec2 operator/(float scalar) const {
            Vec2 result = *this;
            result /= scalar;
            return result;
        }

        constexpr Vec2 &operator-=(const Vec2 &rhs) noexcept {
            x -= rhs.x;
            y -= rhs.y;
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

        constexpr Vec2 &operator*=(float scalar) noexcept {
            x *= scalar;
            y *= scalar;
            return *this;
        }

        constexpr Vec2 operator*(float scalar) const noexcept {
            Vec2 result = *this;
            result *= scalar;
            return result;
        }

        friend constexpr Vec2 operator*(float scalar, const Vec2 &vec) noexcept {
            return vec * scalar;
        }

        [[nodiscard]] float length() const noexcept {
            return std::sqrt(lengthSquared());
        }

        [[nodiscard]] Vec2 normalized() const noexcept {
            const float magnitude = length();

            if (magnitude == 0.0f) {
                return {};
            }

            return {x / magnitude, y / magnitude};
        }

        [[nodiscard]] float dot(const Vec2 &other) const noexcept {
            return x * other.x + y * other.y;
        }

        [[nodiscard]] float lengthSquared() const noexcept {
            return x * x + y * y;
        }
    };
}
