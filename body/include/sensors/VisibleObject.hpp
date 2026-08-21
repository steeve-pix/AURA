#pragma once

#include "world/CellType.hpp"
#include "world/Position.hpp"

namespace aura::sensors {
    /// Actionable sensed object with navigation information from AURA's position.
    struct VisibleObject {
        /// Object category, currently Battery or Unknown.
        world::CellType type;
        /// Absolute world coordinates of the object.
        world::Position position;

        /// Whether the pathfinder found a route to the object's cell.
        bool reachable;
        /// Number of path steps, or -1 when no route exists.
        int pathLength;

        std::optional<world::Position> nextStep{};
    };
}
