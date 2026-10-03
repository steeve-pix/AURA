#pragma once
#include "RigidBody3D.hpp"

namespace aura::physics {
    void integrateLinearMotion(RigidBody3D &body, float dt);

    void integrateAngularMotion(RigidBody3D &body, float dt);

    void updateLinearAcceleration(RigidBody3D &body);

    void updateAngularAcceleration(RigidBody3D &body);

    void clearForce(RigidBody3D &body);

    void clearTorque(RigidBody3D &body);
}
