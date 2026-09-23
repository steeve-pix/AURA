#pragma once
#include "Body2D.hpp"
#include "Torque.hpp"

namespace aura::physics {
    inline void applyForce(Body2D &body, const math::Vec2 &force) {
        body.force += force;
    }

    inline void updateAccelerationFromForce(Body2D &body) {
        body.acceleration = {body.force / body.mass};
    }

    inline void clearForces(Body2D &body) noexcept {
        body.force = {};
    }

    inline void applyGravity(Body2D &body, const math::Vec2 &gravity) noexcept {
        applyForce(body, gravity * body.mass);
    }

    inline void applyHorizontalDrag(Body2D &body, float dragCoefficient) noexcept {
        applyForce(body, {-body.velocity.x * dragCoefficient, 0.0});
    }

    inline void applyForceAtPoint(Body2D &body, const math::Vec2 &force, const math::Vec2 &worldPoint) noexcept {
        applyForce(body, force);

        const math::Vec2 offset =
                worldPoint - body.position;

        const float generatedTorque =
                offset.x * force.y
                - offset.y * force.x;

        applyTorque(body, generatedTorque);
    }
}
