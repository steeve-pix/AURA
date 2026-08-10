#pragma once

#include "agent/Agent.hpp"
#include "world/World.hpp"

namespace aura::render {
    class GridRenderer {
    public:
        static void render(const world::World& world, const agent::Agent& agent) ;
    };
}