#pragma once
#include "Body2D.hpp"

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
    inline void applyHorizontalDrag(Body2D& body,float dragCoefficient) noexcept {
        applyForce(body,{-body.velocity.x * dragCoefficient, 0.0});
    }
}
