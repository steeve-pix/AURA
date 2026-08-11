#pragma once

#include <optional>

#include "bridge/Action.hpp"
#include "sensors/LocalObservation.hpp"
#include "sensors/RangeObservation.hpp"
#include "world/Position.hpp"

namespace aura::bridge {
    /// Outcome of the most recent intention executed by the body.
    struct LastAction {
        ActionType type;
        std::optional<world::Position> target;
        bool succeeded;
    };

    /// Snapshot of physical state sent from the C++ body to the Python brain.
    struct Observation {
        /// Current body location in world coordinates.
        world::Position position;
        /// Remaining movement energy.
        int energy;
        /// Cell types immediately north, east, south, and west of AURA.
        sensors::LocalObservation surroundings;
        sensors::RangeObservation nearby;
        int sensor_radius;
        std::optional<LastAction> lastAction;
    };
}
