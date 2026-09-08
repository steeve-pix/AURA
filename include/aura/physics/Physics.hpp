#pragma once
#include "Body2D.hpp"
#include "Collision.hpp"
#include "Motion.hpp"
#include "World2D.hpp"

namespace aura::physics {
    inline void stepBody(Body2D &body, const World2D &world, float dt) noexcept {
        body.acceleration = world.gravity;

        integrate(body, dt);

        resolveFloorCollision(body, world);
    };
}
