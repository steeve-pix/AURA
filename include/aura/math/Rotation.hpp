#pragma once
#include "Vec3.hpp"

namespace aura::math {
    Vec3 rotateAroundY(const Vec3 &vec, float angleRadians);

    Vec3 rotateAroundZ(const Vec3 &vec, float angleRadians);

    Vec3 rotateAroundX(const Vec3 &vec, float angleRadians);
}
