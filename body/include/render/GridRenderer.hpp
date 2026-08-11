#pragma once

#include "agent/Agent.hpp"
#include "world/World.hpp"

namespace aura::render {
    class GridRenderer {
    public:
        void render(const world::World &world, const agent::Agent &agent, int sensorRadius) const;
    };
}
