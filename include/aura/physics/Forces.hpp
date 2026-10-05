#pragma once
#include "RigidBody3D.hpp"

namespace aura::physics {
    void applyForce(RigidBody3D& body,const math::Vec3& force);
    void applyForceAtPoint(RigidBody3D&body,const math::Vec3& force,const math::Vec3& worldPoint);
}
