#pragma once
#include <array>

#include "aura/math/Vec3.hpp"
#include "aura/physics/RigidBody3D.hpp"

namespace aura::physics {
    std::array<math::Vec3, 8> cubeCorners(const RigidBody3D &body, const math::Vec3 &size);

    math::Vec3 lowestPoint(const RigidBody3D &body, const math::Vec3 &size);
}
