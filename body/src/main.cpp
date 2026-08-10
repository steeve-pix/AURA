#include <iostream>
#include <exception>

#include "agent/Agent.hpp"
#include "bridge/Action.hpp"
#include "bridge/ActionParser.hpp"
#include "bridge/ActionUtils.hpp"
#include "bridge/BrainProcess.hpp"
#include "bridge/Observation.hpp"
#include "bridge/ObservationSerializer.hpp"
#include "navigation/Pathfinder.hpp"
#include "render/TerminalRenderer.hpp"
#include "sensors/LocalSensor.hpp"
#include "sensors/RangeSensor.hpp"
#include "world/CellType.hpp"
#include "world/Position.hpp"
#include "world/World.hpp"

int main() {
    using aura::world::World;
    using aura::render::TerminalRenderer;
    using aura::agent::Agent;
    using aura::sensors::LocalSensor;
    using aura::sensors::RangeSensor;
    using aura::bridge::Observation;
    using aura::bridge::serializedObservation;

    std::cout << "AURA body starting...\n\n";

    // Build a small deterministic scene for exercising the body-brain loop.
    World world{10, 5};
    world.addBoundaryWalls();
    world.setCell({8, 2}, aura::world::CellType::Battery);
    world.setCell({5, 1}, aura::world::CellType::Wall);
    world.setCell({5, 2}, aura::world::CellType::Wall);

    Agent agent{{1, 2}};
    RangeSensor rangeSensor{10};

    const auto path = aura::navigation::findPath(world, agent.position(), {8, 2});

    const auto next = path.front();

    const aura::world::Position offset{
        next.x - agent.position().x,
        next.y - agent.position().y
    };

    static_cast<void>(agent.moveBy(offset, world));

    for (const auto &position: path) {
        std::cout << "(" << position.x << "," << position.y << ")\n";
    }

#if defined(_WIN32)
    const std::string pythonExecutable = "python";
    const std::string scriptPath = "-m brain.main";
    const std::string workingDirectory = R"(C:\Users\Steeve Dim\Documents\AURA)";
#else
    const std::string pythonExecutable = "python3";

    const std::string scriptPath =
            "/Users/steeve.dimitry/Documents/Developer/AURA/brain/main.py";

    const std::string workingDirectory =
            "/Users/steeve.dimitry/Documents/Developer/AURA";
#endif

    aura::bridge::BrainProcess brain{
        pythonExecutable,
        scriptPath,
        workingDirectory
    };

    if (!brain.launch()) {
        std::cerr << "Failed to launch Python brain\n";
        return 1;
    }

    for (int step = 0; step < 10; ++step) {
        const auto rangeObservation = rangeSensor.observe(world, agent);

        for (const auto &object: rangeObservation.objects) {
            std::cout << "RangeSensor detected object at ("
                    << object.position.x << ", "
                    << object.position.y << ")\n";
        }

        // The body senses physical state before serializing it for the Python brain.
        const auto local =
                LocalSensor::observe(world, agent);

        const auto nearby =
                rangeSensor.observe(world, agent);

        Observation observation{
            agent.position(),
            agent.energy(),
            local,
            nearby
        };

        const std::string observationJson =
                serializedObservation(observation);

        std::cout << "Sending to brain: " << observationJson << '\n';

        // exchange() sends exactly one observation line and waits for one action line.
        const std::string actionJson =
                brain.exchange(observationJson);

        if (actionJson.empty()) {
            std::cerr << "Brain returned empty response; skipping this step\n";
            continue;
        }

        std::cout << "Brain response: " << actionJson << '\n';

        aura::bridge::Action action{};
        try {
            action = aura::bridge::parseAction(actionJson);
        } catch (const std::exception &error) {
            std::cerr << "Invalid JSON from brain: " << actionJson << '\n';
            std::cerr << "Reason: " << error.what() << '\n';
            continue;
        }

        if (action.type == aura::bridge::ActionType::Move) {
            static_cast<void>(agent.moveBy(aura::bridge::directionOffset(action.direction), world));
        }

        if (action.type == aura::bridge::ActionType::MoveTo) {
            const auto path = aura::navigation::findPath(
                world,
                agent.position(),
                action.target
            );

            if (!path.empty()) {
                const auto current = agent.position();
                const auto next = path.front();

                const aura::world::Position offset{
                    next.x - current.x,
                    next.y - current.y
                };

                static_cast<void>(agent.moveBy(offset, world));
            }
        }

        TerminalRenderer renderer;
        renderer.render(world, agent);

        std::cout << "Agent energy: " << agent.energy() << '\n';
    }

    return 0;
}
