#pragma once

#include <cstdint>

#include "world/World.hpp"

namespace aura::world
{
    /// Builds reproducible connected mazes and populates their walkable passages.
    class MazeGenerator
    {
    public:
        /// Stores the random seed used for every generated maze.
        explicit MazeGenerator(std::uint32_t seed);

        /// Rebuilds the world and places the requested batteries and unknown objects.
        ///
        /// Object placement is limited to carved passage cells and is deterministic
        /// for a given seed, world size, and pair of object counts.
        void generate(
            World& world,
            int batteryCount,
            int unknownCount,
            int guaranteedBatteryMaximumDistance = 0
        ) const;

    private:
        std::uint32_t seed_;
    };
}
