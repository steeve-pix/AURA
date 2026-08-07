#pragma once

#include "world/Position.hpp"

namespace aura::agent{
    class Agent {
 public:
        explicit Agent(world::Position position);

        world::Position position ()const;

    private:
        world::Position position_;
    };
}


