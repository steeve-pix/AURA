#pragma once

#include "world/CellType.hpp"

namespace aura::sensors {
    /// Cell types immediately adjacent to AURA in cardinal order.
    struct LocalObservation {
        world::CellType north;
        world::CellType east;
        world::CellType south;
        world::CellType west;
    };
}
