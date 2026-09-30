#include "aura/math/Quaternion.hpp"
#include <cmath>

namespace aura::math {
    Quaternion::Quaternion(float w, float x, float y, float z)
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
        return Quaternion{
            w * other.w
            - x * other.x
            - y * other.y
            - z * other.z,

            w * other.x
            + x * other.w
            + y * other.z
            - z * other.y,

            w * other.y
            - x * other.z
            + y * other.w
            + z * other.x,

            w * other.z
            + x * other.y
            - y * other.x
            + z * other.w
        };
    }

    Quaternion Quaternion::conjugate() const {
        return Quaternion{w, -x, -y, -z};
    }

    Vec3 Quaternion::rotate(const Vec3 &vec) const {
        const Quaternion vectorQuaternion{0.0f, vec.x, vec.y, vec.z};

        const Quaternion result = *this * vectorQuaternion * conjugate();

        return Vec3{
            result.x, result.y, result.z
        };
    }

    float Quaternion::lengthSquared() const {
        return w * w + x * x + y * y + z * z;
    }

    float Quaternion::length() const {
        return std::sqrt(lengthSquared());
    }

    Quaternion Quaternion::normalized() const {
        const float len = length();

        return len == 0.0f
                   ? Quaternion{}
                   : Quaternion{
                       w / len,
                       x / len,
                       y / len,
                       z / len
                   };
    }
}
