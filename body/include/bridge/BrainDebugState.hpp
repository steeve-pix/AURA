#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "world/Position.hpp"

namespace aura::bridge {
    struct BrainDebugState {
        std::string goal;

        std::unordered_map<std::string, double> goalScores;

        std::vector<world::Position> knownCells;
        std::vector<world::Position> visitedCells;
    };
}
