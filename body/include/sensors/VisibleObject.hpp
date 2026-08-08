#pragma once

#include "world/CellType.hpp"
#include "world/Position.hpp"

namespace aura::sensors {
    struct VisibleObject {
        world::CellType type;
        world::Position position;
    };
}