#include "aura/physics/Inertia.hpp"

namespace aura::physics {
    math::Vec3 applyInverseInertiaWorld(const RigidBody3D &body, const math::Vec3 &worldVector) {
        const auto orientation = body.orientation.normalized();
        const auto local = orientation.conjugate().rotate(worldVector);
        const math::Vec3 response{local.x / body.momentOfInertia.x,
                                  local.y / body.momentOfInertia.y,
                                  local.z / body.momentOfInertia.z};
        return orientation.rotate(response);
    }
    math::Vec3 boxMomentOfInertia(float mass, const math::Vec3 &size) {
        const float w = size.x;
        const float h = size.y;
        const float d = size.z;

        return {
            (mass / 12.0f) * (h * h + d * d),
            (mass / 12.0f) * (w * w + d * d),
            (mass / 12.0f) * (w * w + h * h)
        };
    }
}
