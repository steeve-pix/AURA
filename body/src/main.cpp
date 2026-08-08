#include <iostream>

#include "agent/Agent.hpp"
#include "bridge/Action.hpp"
#include "bridge/ActionParser.hpp"
#include "bridge/ActionUtils.hpp"
#include "bridge/BrainProcess.hpp"
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

    // Build a small deterministic scene for exercising the body-brain loop.
    World world{10, 5};
    world.addBoundaryWalls();
    world.setCell({3, 2}, CellType::Battery);

    Agent agent{{2, 2}};

    // The body senses physical state before serializing it for the Python brain.
    const auto surroundings = LocalSensor::observe(world, agent);

    Observation observation{
        agent.position(),
        agent.energy(),
        surroundings
    };

    const std::string observationJson =
            serializedObservation(observation);

    aura::bridge::BrainProcess brain{
        "python", "-m brain.main", R"(C:\Users\Steeve Dim\Documents\AURA)"
    };

    if (!brain.launch()) {
        std::cerr << "Failed to launch Python brain\n";
        return 1;
    }

    std::cout << "Sending to brain: " << observationJson << '\n';

    // exchange() sends exactly one observation line and waits for one action line.
    const std::string actionJson =
            brain.exchange(observationJson);
    
    std::cout << "Brain response: " << actionJson << '\n';
    
    const auto action = 
        aura::bridge::parseAction(actionJson);
    
    if(action.type==aura::bridge::ActionType::Move){
        static_cast<void>(agent.moveBy(aura::bridge::directionOffset(action.direction),world));
    }

    TerminalRenderer renderer;
    renderer.render(world, agent);
    
    return 0;
}
