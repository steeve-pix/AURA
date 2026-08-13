#pragma once

#include "agent/Agent.hpp"
#include "world/World.hpp"

namespace aura::render {
    /// Produces a text-only view of the simulation for terminal debugging.
    class TerminalRenderer {
    public:
        /// Prints every world cell while drawing AURA over its current position.
        void render(const world::World &world, const agent::Agent agent) const;
    };
}
