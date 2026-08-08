#pragma once

#include "world/Position.hpp"
#include "world/World.hpp"

namespace aura::agent {
    /// Represents AURA's physical state inside the simulated world.
    ///
    /// The body owns movement, collision checks, energy use, and physical interactions.
    /// High-level decisions remain in the Python brain.
    class Agent {
    public:
        /// Creates an agent at the given grid position with full energy.
        explicit Agent(world::Position position);

        /// Returns the agent's current grid position.
        [[nodiscard]] world::Position position() const;

        /// Attempts one cardinal step and returns whether the body completed it.
        [[nodiscard]] bool moveBy(world::Position offset, const world::World &world);

        /// Returns the agent's current energy.
        [[nodiscard]] int energy() const;

        /// Returns the energy level restored by a battery.
        [[nodiscard]] int maxEnergy() const;

    private:
        world::Position position_;
        int energy_;
        int maxEnergy_;

        /// Applies the physical effect of the cell occupied after movement.
        void interactWithCell(const world::World &world);
    };
}
