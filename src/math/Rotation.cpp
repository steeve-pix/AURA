#include "aura/math/Rotation.hpp"

namespace aura::math {
    Vec3 rotateAroundY(const Vec3 &vec, const float angleRadians) {
        const float c = std::cos(angleRadians);
        const float s = std::sin(angleRadians);

        return {
            vec.x * c + vec.z * s,
            vec.y,
            -vec.x * s + vec.z * c
        };
    }

    Vec3 rotateAroundZ(const Vec3 &vec, const float angleRadians) {
        const float c = std::cos(angleRadians);
        const float s = std::sin(angleRadians);

        return {
            vec.x * c - vec.y * s,
            vec.x * s + vec.y * c,
            vec.z
        };
    }

    Vec3 rotateAroundX(const Vec3 &vec, const float angleRadians) {
        const float c = std::cos(angleRadians);
        const float s = std::sin(angleRadians);

        return {
            vec.x,
            vec.y * c - vec.z * s,
            vec.y * s + vec.z * c
        };
    }
}
