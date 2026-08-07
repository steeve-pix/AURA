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
    world.setCell({7, 3}, CellType::Battery);

    Agent agent{{5, 1}};
    
    // if (!agent.moveTo({3, 2}, world)) {
        // std::cerr << "AURA could not move to (3, 2).\n";
        // return 1;
    // }

    TerminalRenderer renderer;
    renderer.render(world, agent);

    return 0;
}
