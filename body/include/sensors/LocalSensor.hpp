#pragma once

#include "agent/Agent.hpp"
#include "sensors/LocalObservation.hpp"
#include "world/World.hpp"

namespace aura::sensors
{
    /// Reads the four cells that share an edge with AURA's current position.
    class LocalSensor
    {
    public:
        /// Returns neighboring cell contents in north, east, south, west order.
        static LocalObservation observe(const world::World& world, const agent::Agent& agent);
    };
}
