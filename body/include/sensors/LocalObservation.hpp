#pragma once

#include "world/CellType.hpp"

namespace aura::sensors {
    struct LocalObservation {
        CellType north;
        CellType east;
        CellType south;
        CellType west;
    };
}
