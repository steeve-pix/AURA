#pragma once

#include "world/CellType.hpp"

namespace aura::sensors
{
    /// Contents of the four cells sharing an edge with AURA's current cell.
    struct LocalObservation
    {
        world::CellType north;
        world::CellType east;
        world::CellType south;
        world::CellType west;
    };
}
