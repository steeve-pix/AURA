#pragma once

#include "sensors/LocalObservation.hpp"
#include "world/Position.hpp"

namespace aura::bridge {
    struct Observation {
        world::Position position;
        int energy;
        sensors::LocalObservation surroundings;
    };
}
