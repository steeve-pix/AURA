#pragma once

#include <string>
#include <vector>

#include "world/Position.hpp"

namespace aura::bridge {
    struct BrainDebugState {
        std::string goal;

        std::vector<world::Position> knownCells;
        std::vector<world::Position> visitedCells;
    };
}
