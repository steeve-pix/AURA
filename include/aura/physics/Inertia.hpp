#pragma once
#include "aura/math/Vec3.hpp"
#include "RigidBody3D.hpp"

namespace aura::physics {
    math::Vec3 boxMomentOfInertia(float mass, const math::Vec3 &size);
    // Principal moments are local; vector and result are world-space.
    // Requires positive principal moments on a dynamic body.
    math::Vec3 applyInverseInertiaWorld(const RigidBody3D &body, const math::Vec3 &worldVector);
}
