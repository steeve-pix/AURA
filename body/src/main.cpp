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

    Agent agent{{2, 2}};
    
    std::cout << "Energy: " << agent.energy() << '\n';

    agent.moveBy({1, 0}, world);
    std::cout << "Energy: " << agent.energy() << '\n';

    agent.moveBy({0, 1}, world);
    std::cout << "Energy: " << agent.energy() << "\n\n";

    TerminalRenderer renderer;
    renderer.render(world, agent);

    return 0;
}
