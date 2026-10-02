#pragma once
#include "RigidBody3D.hpp"

namespace aura::physics {
    void integrateLinearMotion(RigidBody3D &body, float dt);

    void integrateAngularMotion(RigidBody3D &body, float dt);

    void updateAngularAcceleration(RigidBody3D &body);

    void clearTorque(RigidBody3D &body);
}
