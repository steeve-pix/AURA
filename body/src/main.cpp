#include <iostream>

#include "render/TerminalRenderer.hpp"
#include "world/World.hpp"

int main() {
    using aura::world::World;
    using aura::render::TerminalRenderer;

    std::cout << "AURA body starting...\n\n";
    
    World world{10, 5};
    
    world.addBoundaryWalls();
    world.setCell({3, 2}, CellType::Wall);
    world.setCell({7, 3}, CellType::Battery);
    
    TerminalRenderer renderer;
    renderer.render(world);
    
    return 0;
}
