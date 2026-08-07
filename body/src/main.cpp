#include <iostream>

#include "agent/Agent.hpp"
#include "render/TerminalRenderer.hpp"
#include "sensors/LocalSensor.hpp"
#include "world/CellTypeUtils.hpp"
#include "world/World.hpp"

int main() {
    using aura::world::World;
    using aura::render::TerminalRenderer;
    using aura::agent::Agent;
    using aura::sensors::LocalSensor;
    using aura::world::toString;

    std::cout << "AURA body starting...\n\n";

    World world{10, 5};
    world.addBoundaryWalls();
//    world.setCell({4, 2}, CellType::Battery);
//    world.setCell({4, 1}, CellType::Wall);
    world.setCell({5, 2}, CellType::Battery);

    Agent agent{{2, 2}};

    static_cast<void>(agent.moveBy({1, 0}, world)); // 99
    static_cast<void>(agent.moveBy({1, 0}, world)); // reaches battery → 100

    TerminalRenderer renderer;
    renderer.render(world, agent);

    const auto observation = LocalSensor::observe(world, agent);

    std::cout << "\nLocal sensor readings:\n"
              << "North: " << toString(observation.north) << "\n"
              << "East: " << toString(observation.east) << "\n"
              << "South: " << toString(observation.south) << "\n"
              << "West: " << toString(observation.west) << "\n";

    return 0;
}
