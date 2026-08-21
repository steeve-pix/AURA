#pragma once

#include <vector>

#include "world/Position.hpp"
#include "world/World.hpp"

namespace aura::navigation
{
    /// Finds the shortest traversable route between two world positions.
    ///
    /// The returned path excludes `start`, includes `goal`, and is empty when the
    /// destination cannot be reached. Each entry differs from the previous one by
    /// exactly one cardinal step.
    std::vector<aura::world::Position> findPath(
        const aura::world::World& world,
        aura::world::Position start,
        aura::world::Position goal
    );
}
