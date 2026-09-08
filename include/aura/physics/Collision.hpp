#pragma once
#include "Body2D.hpp"
#include "BodyGeometry.hpp"
#include "World2D.hpp"

namespace aura::physics {
    inline bool intersectsFloor(const Body2D &body, const World2D &world) noexcept {
        return bottom(body) < world.floorHeight;
    }

    inline void correctFloorPenetration(Body2D &body, const World2D &world) noexcept {
        if (!intersectsFloor(body, world)) {
            return;
        }

        body.position.y =
                world.floorHeight + body.size.y * 0.5f;
    }

    inline void stopDownwardVelocity(Body2D &body) noexcept {
        if (body.velocity.y < 0.0f)
            body.velocity.y = 0.0f;
    }

    inline void resolveFloorCollision(Body2D &body, const World2D &world) noexcept {
        if (!intersectsFloor(body, world)) {
            return;
        }

        correctFloorPenetration(body, world);
        stopDownwardVelocity(body);
    }
}
