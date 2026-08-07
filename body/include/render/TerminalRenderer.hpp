#pragma once

#include "agent/Agent.hpp"
#include "world/World.hpp"

namespace aura::render {
    class TerminalRenderer {
    public:
        void render(const world::World &world, const agent::Agent agent) const;
    };
}
