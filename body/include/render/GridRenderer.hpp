#pragma once

#include <vector>

#include "agent/Agent.hpp"
#include "bridge/BrainDebugState.hpp"
#include "world/Position.hpp"
#include "world/World.hpp"

namespace aura::render {
    class GridRenderer {
    public:
        static void render(const world::World &world, const agent::Agent &agent, int sensorRadius,
                           const std::vector<world::Position> &path, const world::Position *target, const bridge::BrainDebugState& debug);
    };
}
