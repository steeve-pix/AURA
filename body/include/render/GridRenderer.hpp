#pragma once

#include <vector>

#include "agent/Agent.hpp"
#include "bridge/BrainDebugState.hpp"
#include "world/Position.hpp"
#include "world/World.hpp"

namespace aura::render {
    /// Draws the world, agent, navigation route, and brain debug overlays with OpenGL.
    class GridRenderer {
    public:
        /// Renders one frame without mutating simulation state.
        ///
        /// `target` may be null when no route is active. `sensorRadius` is measured
        /// in cells and controls the visible sensor overlay around AURA.
        static void render(const world::World &world, const agent::Agent &agent, int sensorRadius,
                           const std::vector<world::Position> &path, const world::Position *target,
                           const bridge::BrainDebugState &debug);
    };
}
