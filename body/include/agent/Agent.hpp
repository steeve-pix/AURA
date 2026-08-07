#pragma once

#include "world/Position.hpp"
#include "world/World.hpp"

namespace aura::agent {
    class Agent {
    public:
        explicit Agent(world::Position position);

        [[nodiscard]] world::Position position() const;

        [[nodiscard]] bool moveBy(world::Position offset, const world::World &world);

        [[nodiscard]] int energy() const;

        [[nodiscard]] int maxEnergy() const;

    private:
        world::Position position_;
        int energy_;
        int maxEnergy_;
    };
}
