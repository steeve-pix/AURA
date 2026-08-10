#pragma once

#include <vector>

#include "world/Position.hpp"
#include "world/World.hpp"

namespace aura::navigation {
    std::vector<aura::world::Position> findPath(
        const aura::world::World &world,
        aura::world::Position start,
        aura::world::Position goal
    );
}