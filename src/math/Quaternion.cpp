#include "aura/math/Quaternion.hpp"
#include <cmath>

namespace aura::math {
    Quaternion::Quaternion(double w, double x, double y, double z)
        : w(w), x(x), y(y), z(z) {
    }

    Quaternion Quaternion::fromAxisAngle(const Vec3 &axis, float angleRadians) {
        const Vec3 normalizedAxis =
                axis.normalized();

        const float halfAngle =
                angleRadians / 2.0f;

        const float s =
                std::sin(halfAngle);

        return {
            std::cos(halfAngle),
            normalizedAxis.x * s,
            normalizedAxis.y * s,
            normalizedAxis.z * s
        };
    }

    Quaternion Quaternion::operator*(const Quaternion &other) const {
        // Orientations retain double precision: common component rotations must
        // preserve descendant twists even near the swing/twist singularity.
        const double aw = w, ax = x, ay = y, az = z;
        const double bw = other.w, bx = other.x, by = other.y, bz = other.z;
        return {
            (aw * bw - ax * bx - ay * by - az * bz),
            (aw * bx + ax * bw + ay * bz - az * by),
            (aw * by - ax * bz + ay * bw + az * bx),
            (aw * bz + ax * by - ay * bx + az * bw)
        };
    }

    Quaternion Quaternion::conjugate() const {
        return Quaternion{w, -x, -y, -z};
    }

    Vec3 Quaternion::rotate(const Vec3 &vec) const {
        const Quaternion vectorQuaternion{0.0f, vec.x, vec.y, vec.z};

        const Quaternion result = *this * vectorQuaternion * conjugate();

        return Vec3{
            static_cast<float>(result.x), static_cast<float>(result.y), static_cast<float>(result.z)
        };
    }

    double Quaternion::lengthSquared() const {
        return w * w + x * x + y * y + z * z;
    }

    double Quaternion::length() const {
        return std::sqrt(lengthSquared());
    }

    Quaternion Quaternion::normalized() const {
        const double len = length();

        return len == 0.0
                   ? Quaternion{}
                   : Quaternion{
                       w / len,
                       x / len,
                       y / len,
                       z / len
                   };
    }
}
