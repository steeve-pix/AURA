#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "world/Position.hpp"

namespace aura::bridge {
    /// Optional planning details returned with an action for developer visualization.
    ///
    /// Debug state is presentation data only and must not influence body behavior.
    struct BrainDebugState {
        /// Name of the goal currently selected by the brain.
        std::string goal;

        /// Score assigned to each candidate goal during the latest decision.
        std::unordered_map<std::string, double> goalScores;

        /// Cells represented in the brain's accumulated world memory.
        std::vector<world::Position> knownCells;
        /// Cells AURA has physically occupied at least once.
        std::vector<world::Position> visitedCells;

        /// Active plan summary for developer-facing runtime diagnostics.
        std::string planGoal;
        int planCurrentStep = 0;
        int planStepCount = 0;
        bool planFailed = false;
        std::string planStepType;
        world::Position planStepTarget{};
        bool hasPlanStepTarget = false;
    };
}
