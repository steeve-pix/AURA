#pragma once

#include "agent/Agent.hpp"
#include "world/World.hpp"

namespace aura::simulation {
    /// Complete physical state needed to replay a simulation step.
    struct SimulationSnapshot {
        world::World world;
        agent::Agent agent;
    };

    /// Captures independent copies of the body's current physical state.
    [[nodiscard]] SimulationSnapshot captureSimulationSnapshot(
        const world::World &world,
        const agent::Agent &agent
    );

    /// Replaces the body's current physical state with a previously captured state.
    void restoreSimulationSnapshot(
        world::World &world,
        agent::Agent &agent,
        const SimulationSnapshot &snapshot
    );
}
