#pragma once
#include "RigidBody3D.hpp"

namespace aura::physics {
    void applyImpulse(RigidBody3D &body, const math::Vec3 &impulse);

    void applyImpulseAtPoint(RigidBody3D &body, const math::Vec3 &impulse, const math::Vec3 &worldPoint);
}
