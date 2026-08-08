#pragma once

#include "sensors/LocalObservation.hpp"
#include "world/Position.hpp"

namespace aura::bridge {
    /// Snapshot of physical state sent from the C++ body to the Python brain.
    struct Observation {
        /// Current body location in world coordinates.
        world::Position position;
        /// Remaining movement energy.
        int energy;
        /// Cell types immediately north, east, south, and west of AURA.
        sensors::LocalObservation surroundings;
    };
}
