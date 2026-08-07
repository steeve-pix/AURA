#pragma once

#include "world/Position.hpp"
#include "world/World.hpp"

namespace aura::agent {
    class Agent {
    public:
        explicit Agent(world::Position position);

        [[nodiscard]] world::Position position() const;

        [[nodiscard]] bool moveTo(world::Position position, const world::World& world);

    private:
        world::Position position_;
    };
}
