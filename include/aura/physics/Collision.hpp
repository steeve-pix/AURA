#pragma once
#include "Body2D.hpp"
#include "BodyGeometry.hpp"
#include "World2D.hpp"

namespace aura::physics {
    inline bool intersectsFloor(const Body2D &body, const World2D &world) noexcept {
        return bottom(body) < world.floorHeight;
    }
}
