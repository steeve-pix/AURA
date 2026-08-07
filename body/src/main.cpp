#include <iostream>

#include "agent/Agent.hpp"
#include "render/TerminalRenderer.hpp"
#include "world/World.hpp"

int main() {
    using aura::world::World;
    using aura::render::TerminalRenderer;
    using aura::agent::Agent;

    std::cout << "AURA body starting...\n\n";

    World world{10, 5};
    world.addBoundaryWalls();
    world.setCell({4, 2}, CellType::Battery);

    Agent agent{{2, 2}};

    static_cast<void>(agent.moveBy({1, 0}, world)); // 99
    static_cast<void>(agent.moveBy({1, 0}, world)); // reaches battery → 100

    TerminalRenderer renderer;
    renderer.render(world, agent);

    return 0;
}
