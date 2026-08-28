#pragma once

#include <string>

#include "agent/Agent.hpp"
#include "bridge/Action.hpp"
#include "world/World.hpp"

namespace aura::simulation {
    /// Immediate physical outcome of an action evaluated without committing it.
    struct CounterfactualResult {
        bool succeeded;
        std::string result;
        world::Position positionAfter;
        int energyAfter;
    };

    /// Executes one action against a temporary physical branch and restores state.
    [[nodiscard]] CounterfactualResult simulateAction(
            world::World &world,
            agent::Agent &agent,
            const bridge::Action &action,
            world::CellType investigationOutcome = world::CellType::Empty
    );
}
