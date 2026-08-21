#pragma once

#include "world/CellType.hpp"
#include "world/Position.hpp"

namespace aura::sensors
{
    /// Cell content paired with the world position at which it was observed.
    struct VisibleCell
    {
        /// Physical content observed in the cell.
        world::CellType type;
        /// Absolute world coordinates of the cell.
        world::Position position;
    };
}
