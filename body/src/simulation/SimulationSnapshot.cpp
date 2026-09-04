#include "simulation/SimulationSnapshot.hpp"

namespace aura::simulation {
    SimulationSnapshot captureSimulationSnapshot(
        const world::World &world,
        const agent::Agent &agent
    ) {
        return {world, agent};
    }

    void restoreSimulationSnapshot(
        world::World &world,
        agent::Agent &agent,
        const SimulationSnapshot &snapshot
    ) {
        world = snapshot.world;
        agent = snapshot.agent;
    }
}
