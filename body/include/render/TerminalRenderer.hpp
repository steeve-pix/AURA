#pragma once

#include "agent/Agent.hpp"
#include "world/World.hpp"

namespace aura::render {
    /// Draws a text-only view of the current world for the developer.
    class TerminalRenderer {
    public:
        /// Prints the grid and overlays AURA at its current position.
        void render(const world::World &world, const agent::Agent agent) const;
    };
}
