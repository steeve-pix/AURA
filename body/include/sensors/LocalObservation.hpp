#pragma once

#include "world/CellType.hpp"

namespace aura::sensors {
    /// Cell types immediately adjacent to AURA in cardinal order.
    struct LocalObservation {
        CellType north;
        CellType east;
        CellType south;
        CellType west;
    };
}
