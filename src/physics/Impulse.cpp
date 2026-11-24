#include "aura/physics/Impulse.hpp"
#include "aura/physics/Inertia.hpp"

namespace aura::physics {
    void applyImpulse(RigidBody3D &body, const math::Vec3 &impulse) {
        body.velocity += impulse * (1.0f / body.mass);
    }

    void applyImpulseAtPoint(RigidBody3D &body, const math::Vec3 &impulse, const math::Vec3 &worldPoint) {
        body.velocity +=
                impulse * (1.0f / body.mass);

        const math::Vec3 r =
                worldPoint - body.position;

        const math::Vec3 angularImpulse =
                r.cross(impulse);

        body.angularVelocity += applyInverseInertiaWorld(body, angularImpulse);
    }
}
