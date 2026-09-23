#pragma once
#include "Body2D.hpp"
#include "aura/math/Vec2.hpp"

namespace aura::physics {
    inline void applyImpulseAtPoint(Body2D &body, const math::Vec2 &impulse, const math::Vec2 &worldPoint) noexcept {
        body.velocity += impulse * (1.0f / body.mass);

        const math::Vec2 offset =
                worldPoint - body.position;

        const float angularImpulse =
                offset.x * impulse.y -
                offset.y * impulse.x;

        body.angularVelocity +=
                angularImpulse / body.momentOfInertia;
    }
}
