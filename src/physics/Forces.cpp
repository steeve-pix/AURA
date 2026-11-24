#include "aura/physics/Forces.hpp"

namespace aura::physics {
    void applyForce(RigidBody3D &body, const math::Vec3 &force) {
        body.force += force;
    }

    void applyForceAtPoint(RigidBody3D &body, const math::Vec3 &force, const math::Vec3 &worldPoint) {
        body.force += force;

        const math::Vec3 r =
                worldPoint - body.position;

        body.torque += r.cross(force);
    }
}
