#pragma once
#include "Body2D.hpp"

namespace aura::physics {
    inline void updatePosition(Body2D &body, float dt) noexcept {
        body.position += body.velocity * dt;
    }

    inline void updateVelocity(Body2D &body, float dt) noexcept {
        body.velocity += body.acceleration * dt;
    }

    inline void integrate(Body2D &body, float dt) noexcept {
        updateVelocity(body, dt);
        updatePosition(body, dt);
        updateAngle(body, dt);
    }
}
