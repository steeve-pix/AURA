#pragma once
#include <array>
#include <vector>

#include "aura/math/Vec3.hpp"
#include "aura/physics/RigidBody3D.hpp"

namespace aura::physics {
    std::array<math::Vec3, 8> cubeCorners(const RigidBody3D &body, const math::Vec3 &size);

    math::Vec3 lowestPoint(const RigidBody3D &body, const math::Vec3 &size);

    // Returns world-space corners at or below floorY + tolerance (tolerance >= 0).
    std::vector<math::Vec3> floorContactPoints(
        const RigidBody3D &body, const math::Vec3 &size, float floorY, float tolerance
    );

    math::Vec3 velocityAtWorldPoint(const RigidBody3D &body, const math::Vec3 &worldPoint);

    std::vector<math::Vec3> floorContactPoints(const RigidBody3D &body, const math::Vec3 &size, float floorY,
                                               float tolerance);
}
