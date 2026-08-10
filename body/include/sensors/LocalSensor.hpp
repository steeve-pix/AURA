#pragma once

#include "agent/Agent.hpp"
#include "sensors/LocalObservation.hpp"
#include "world/World.hpp"

namespace aura::sensors {
    /// Reads the four cells directly adjacent to the agent.
    ///
    /// The sensor reports physical facts only; the Python brain interprets their meaning.
    class LocalSensor {
    public:
        /// Captures the current north, east, south, and west cell types.
        static LocalObservation observe(const world::World &world, const agent::Agent &agent);
    };
}
