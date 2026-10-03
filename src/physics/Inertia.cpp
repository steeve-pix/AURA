#include "aura/physics/Inertia.hpp"

namespace aura::physics {
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
