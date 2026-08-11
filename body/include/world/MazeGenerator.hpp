#pragma once

#include <cstdint>

#include "world/World.hpp"

namespace aura::world {
    /// Creates connected maze passages and places batteries within them.
    class MazeGenerator {
    public:
        /// Uses a fixed seed so the generated world is reproducible.
        explicit MazeGenerator(std::uint32_t seed);

        /// Replaces the world's interior with a connected maze and batteries.
        void generate(World &world, int batteryCount) const;

    private:
        std::uint32_t seed_;
    };
}
