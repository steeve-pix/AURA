#pragma once
#include "aura/math/Vec3.hpp"

namespace aura::physics {
    math::Vec3 boxMomentOfInertia(float mass, const math::Vec3 &size);
}
