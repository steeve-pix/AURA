#include <iostream>

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

    TerminalRenderer renderer;
    renderer.render(world, agent);
    
    return 0;
}
