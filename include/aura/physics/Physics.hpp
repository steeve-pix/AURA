#pragma once
#include "Body2D.hpp"
#include "Collision.hpp"
#include "Forces.hpp"
#include "Motion.hpp"
#include "Torque.hpp"
#include "World2D.hpp"

namespace aura::physics {
    inline void stepBody(Body2D &body, const World2D &world, float dt) noexcept {
        applyGravity(body, world.gravity);

        updateAccelerationFromForce(body);
        updateAngularAccelerationFromTorque(body);

        integrate(body, dt);

        resolveFloorCollision(body, world);

        clearForces(body);
        clearTorque(body);
    };
}
