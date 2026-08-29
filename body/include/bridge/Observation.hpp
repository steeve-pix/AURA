#pragma once

#include <optional>
#include <string>

#include "bridge/Action.hpp"
#include "sensors/LocalObservation.hpp"
#include "sensors/RangeObservation.hpp"
#include "world/Position.hpp"

namespace aura::bridge {
    /// Result of the most recently attempted body action.
    struct LastAction {
        /// Category of action that was attempted.
        ActionType type;
        /// Destination when the action addressed a coordinate; otherwise empty.
        std::optional<world::Position> target;
        /// Whether the body completed the requested action.
        bool succeeded;
        /// Physical result of the attempt: completed, failed, or unreachable.
        std::string result;
        /// Remaining BFS route cost before a navigation action, when reachable.
        std::optional<int> pathLengthBefore{};
        /// Remaining BFS route cost after a navigation action, when reachable.
        std::optional<int> pathLengthAfter{};
        /// First BFS cell selected before executing a navigation action.
        std::optional<world::Position> nextStepBefore{};
        /// First BFS cell for continuing toward the same target next cycle.
        std::optional<world::Position> nextStepAfter{};
        /// Whether the MoveTo target had a valid BFS route before execution.
        std::optional<bool> reachableBefore{};
    };

    /// Read-only world snapshot supplied to the Python brain for one decision cycle.
    struct Observation {
        /// AURA's current world coordinates.
        world::Position position;
        /// Remaining movement energy.
        int energy;
        /// Contents of the four cardinally adjacent cells.
        sensors::LocalObservation surroundings;
        /// Cells and actionable objects reported by the range sensor.
        sensors::RangeObservation nearby;
        /// Half-width of the square area covered by the range sensor.
        int sensor_radius;
        /// Previous action result, absent before the first decision cycle.
        std::optional<LastAction> lastAction;

        std::string worldId;
    };
}
