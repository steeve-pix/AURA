#pragma once
#include "Vec3.hpp"

namespace aura::math {
    // Double storage avoids magnifying float orientation roundoff in joint twist
    // extraction near a perpendicular 180-degree swing. Vec3/render matrices stay float.
    struct Quaternion {
        double w = 1.0f;
        double x = 0.0f;
        double y = 0.0f;
        double z = 0.0f;

        Quaternion() = default;

        Quaternion(double w, double x, double y, double z);

        static Quaternion fromAxisAngle(const Vec3 &axis, float angleRadians);

        Quaternion operator*(const Quaternion &other) const;

        [[nodiscard]] Quaternion conjugate() const;

        [[nodiscard]] Vec3 rotate(const Vec3 &vec) const;

        [[nodiscard]] double lengthSquared() const;

        [[nodiscard]] double length() const;

        [[nodiscard]] Quaternion normalized()const;
    };
}
