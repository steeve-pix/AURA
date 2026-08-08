#include <iostream>

#include "agent/Agent.hpp"
#include "bridge/ActionParser.hpp"
#include "bridge/ActionUtils.hpp"
#include "bridge/Observation.hpp"
#include "bridge/ObservationSerializer.hpp"
#include "render/TerminalRenderer.hpp"
#include "sensors/LocalSensor.hpp"
#include "world/World.hpp"

int main() {
    using aura::world::World;
    using aura::render::TerminalRenderer;
    using aura::agent::Agent;
    using aura::sensors::LocalSensor;
    using aura::bridge::Observation;
    using aura::bridge::serializedObservation;

    std::cout << "AURA body starting...\n\n";

    World world{10, 5};
    world.addBoundaryWalls();
    world.setCell({5, 2}, CellType::Battery);

    Agent agent{{2, 2}};

    static_cast<void>(agent.moveBy({1, 0}, world)); // 99
    static_cast<void>(agent.moveBy({1, 0}, world)); // 98

    const auto action = aura::bridge::parseAction(
        R"({"action": "move","direction": "east"})"
    );

    if (action.type == aura::bridge::ActionType::Move &&
        !agent.moveBy(aura::bridge::directionOffset(action.direction), world)) {
        std::cerr << "AURA could not perform the parsed movement.\n";
        return 1;
    }

    TerminalRenderer renderer;
    renderer.render(world, agent);

    const auto surroundings = LocalSensor::observe(world, agent);

    Observation observation{
        agent.position(),
        agent.energy(),
        surroundings
    };

    std::cout << "\nObservation JSON:\n"
              << serializedObservation(observation) << "\n";

    return 0;
}
