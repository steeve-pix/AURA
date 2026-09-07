#pragma once
#include "Observation.hpp"
#include "agent/Agent.hpp"
#include "sensors/RangeSensor.hpp"
#include "world/World.hpp"
#include <optional>
#include <string>

namespace aura::bridge {
    [[nodiscard]] Observation buildObservation(
        const world::World &world,
        const agent::Agent &agent,
        const sensors::RangeSensor &rangeSensor,
        const std::optional<LastAction> &lastAction,
        const std::string &worldId
    );
}
