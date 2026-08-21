#pragma once

#include "world/Position.hpp"
#include "world/World.hpp"

namespace aura::agent
{
    /// Holds AURA's position and energy in the simulated world.
    ///
    /// Agent validates physical movement and applies cell interactions. It does not
    /// choose destinations; strategic decisions belong to the Python brain.
    class Agent
    {
    public:
        /// Places AURA at `position` with the requested initial energy capacity.
        explicit Agent(
            world::Position position,
            int initialEnergy = 100,
            int maxEnergy = 100
        );

        /// Returns AURA's current grid coordinates.
        [[nodiscard]] world::Position position() const;

        /// Attempts one cardinal, one-cell move.
        ///
        /// The move fails without changing state when the offset is invalid, the
        /// destination is blocked or outside the world, or no energy remains.
        [[nodiscard]] bool moveBy(world::Position offset, const world::World& world);

        /// Returns the movement energy currently available.
        [[nodiscard]] int energy() const;

        /// Returns the energy capacity restored when AURA reaches a battery.
        [[nodiscard]] int maxEnergy() const;

    private:
        world::Position position_;
        int energy_;
        int maxEnergy_;

        /// Applies any effect associated with the cell AURA currently occupies.
        void interactWithCell(const world::World& world);
    };
}
