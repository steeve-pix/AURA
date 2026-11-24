#include "aura/math/Vec3.hpp"

#include <cmath>

namespace aura::math {
    Vec3::Vec3(float xValue, float yValue, float zValue)
        : x{xValue}, y{yValue}, z{zValue} {
    }

    Vec3 Vec3::operator+(const Vec3 &rhs) const {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }

    Vec3 Vec3::operator+=(const Vec3 &rhs) {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }

    Vec3 Vec3::operator-(const Vec3 &rhs) const {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }

    Vec3 Vec3::operator-() const {
        return *this * -1;
    }

    Vec3 Vec3::operator-=(const Vec3 &rhs) {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }

    Vec3 Vec3::operator*(float scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }

    float Vec3::lengthSquared() const {
        return x * x + y * y + z * z;
    }

    float Vec3::length() const {
        return sqrt(lengthSquared());
    }


    Vec3 Vec3::normalized() const {
        const float len = length();

        if (len == 0.0f) {
            return {};
        }
        return Vec3{x / len, y / len, z / len};
    }

    float Vec3::dot(const Vec3 &rhs) const {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }

    Vec3 Vec3::cross(const Vec3 &rhs) const {
        return Vec3{y * rhs.z - z * rhs.y, z * rhs.x - x * rhs.z, x * rhs.y - y * rhs.x};
    }

    Vec3 &Vec3::operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }
}
