#pragma once
#include "Body2D.hpp"
#include "Torque.hpp"

namespace aura::physics {
    inline void updatePosition(Body2D &body, float dt) noexcept {
        body.position += body.velocity * dt;
    }

    inline void updateVelocity(Body2D &body, float dt) noexcept {
        body.velocity += body.acceleration * dt;
    }


    inline void updateAngle(Body2D &body, float dt) noexcept {
        body.angle += body.angularVelocity * dt;
    }

    inline void updateAngularVelocity(Body2D &body, float dt) noexcept {
        body.angularVelocity +=
                body.angularAcceleration * dt;
    }

    inline void integrate(Body2D &body, float dt) noexcept {
        updateVelocity(body, dt);
        updatePosition(body, dt);

        updateAngularVelocity(body, dt);
        updateAngle(body, dt);
    }
}
