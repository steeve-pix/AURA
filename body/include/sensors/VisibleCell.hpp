#pragma once

#include "world/CellType.hpp"
#include "world/Position.hpp"

namespace aura::sensors {
    struct VisibleCell {
        world::CellType type;
        world::Position position;
    };
}
