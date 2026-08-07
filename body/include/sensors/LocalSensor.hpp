#pragma once

#include "agent/Agent.hpp"
#include "sensors/LocalObservation.hpp"
#include "world/World.hpp"

namespace aura::sensors {
    class LocalSensor {
    public:
        static LocalObservation observe(const world::World &world, const agent::Agent &agent);
    };
}
