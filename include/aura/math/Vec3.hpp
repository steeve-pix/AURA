#pragma once

namespace aura::math {
    struct Vec3 {
        float x{}, y{}, z{};

        Vec3() = default;

        Vec3(float xValue, float yValue, float zValue);

        Vec3 operator+(const Vec3 &rhs) const;

        Vec3 operator+=(const Vec3 &rhs);

        Vec3 operator-(const Vec3 &rhs) const;

        Vec3 operator-() const;

        Vec3 operator-=(const Vec3 &rhs);

        Vec3 operator*(float scalar) const;

        [[nodiscard]] float lengthSquared() const;

        [[nodiscard]] float length() const;

        [[nodiscard]] Vec3 normalized() const;

        [[nodiscard]] float dot(const Vec3 &rhs) const;

        [[nodiscard]] Vec3 cross(const Vec3 &rhs) const;

        Vec3 operator*=(const float &scalar) const;
    };
}
