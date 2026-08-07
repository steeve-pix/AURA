#pragma once

#include "world/World.hpp"

namespace aura::render {
    class TerminalRenderer {
    public:
        void render(const aura::world::World& world) const;
    };
}