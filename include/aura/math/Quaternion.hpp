#pragma once
#include "Vec3.hpp"

namespace aura::math {
    struct Quaternion {
        float w = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Quaternion() = default;

        Quaternion(float w, float x, float y, float z);

        static Quaternion fromAxisAngle(const Vec3 &axis, float angleRadians);

        Quaternion operator*(const Quaternion &other) const;

        [[nodiscard]] Quaternion conjugate() const;

        [[nodiscard]] Vec3 rotate(const Vec3 &vec) const;

        [[nodiscard]] float lengthSquared() const;

        [[nodiscard]] float length() const;

        [[nodiscard]] Quaternion normalized() const;
    };
}
