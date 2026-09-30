#include "aura/physics/Motion.hpp"

namespace aura::physics {
    void integrateLinearMotion(RigidBody3D &body, float dt) {
        body.position += body.velocity * dt;
    }
}
