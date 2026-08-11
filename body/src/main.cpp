#include <iostream>
#include <ostream>
#include <vector>
#include <GLFW/glfw3.h>

#include "bridge/Action.hpp"
#include "bridge/ActionParser.hpp"
#include "bridge/ActionUtils.hpp"
#include "bridge/BrainProcess.hpp"
#include "bridge/BrainResponseParser.hpp"
#include "bridge/Observation.hpp"
#include "bridge/ObservationSerializer.hpp"
#include "navigation/Pathfinder.hpp"
#include "render/GridRenderer.hpp"
#include "render/Window.hpp"
#include "sensors/LocalSensor.hpp"
#include "sensors/RangeSensor.hpp"

int main() {
    using aura::render::GridRenderer;
    using aura::render::Window;

    Window window{1280, 720, "AURA"};

    aura::world::World world{10, 5};
    world.addBoundaryWalls();
    world.setCell({6, 3}, aura::world::CellType::Battery);

    aura::agent::Agent agent{{3, 2}};

    aura::sensors::RangeSensor rangeSensor{3};
#if defined(_WIN32)
    const std::string pythonExecutable = "python";
    const std::string scriptPath = "-m brain.main";
    const std::string workingDirectory =
            R"(C:\Users\Steeve Dim\Documents\AURA)";
#else
    const std::string pythonExecutable = "python3";
    const std::string scriptPath = "brain/main.py";
    const std::string workingDirectory =
            "/Users/steeve.dimitry/Developer/AURA";
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

    double lastUpdateTime = glfwGetTime();

    constexpr double updateInterval = 0.25;

    std::vector<aura::world::Position> currentPath;

    aura::world::Position currentTarget{};
    bool hasTarget = false;

    aura::bridge::BrainDebugState brainDebug;

    while (!window.shouldClose()) {
        Window::pollEvents();

        const double now = glfwGetTime();

        if (now - lastUpdateTime >= updateInterval) {
            const auto local =
                    aura::sensors::LocalSensor::observe(
                        world,
                        agent
                    );

            const auto nearby =
                    rangeSensor.observe(
                        world,
                        agent
                    );

            const aura::bridge::Observation observation{
                agent.position(),
                agent.energy(),
                local,
                nearby,
                rangeSensor.radius()
            };

            const std::string observationJson =
                    aura::bridge::serializedObservation(
                        observation
                    );

            const std::string actionJson =
                    brain.exchange(
                        observationJson
                    );

            if (!actionJson.empty()) {
                try {
                    const auto response =
                            aura::bridge::parseBrainResponse(actionJson);

                    brainDebug = response.debug;

                    const auto action =
                            response.action;

                    if (action.type == aura::bridge::ActionType::Move) {
                        currentPath.clear();
                        hasTarget = false;

                        static_cast<void>(agent.moveBy(aura::bridge::directionOffset(action.direction), world));
                    }

                    if (action.type == aura::bridge::ActionType::MoveTo) {
                        currentTarget = action.target;
                        hasTarget = true;

                        currentPath =
                                aura::navigation::findPath(
                                    world,
                                    agent.position(),
                                    currentTarget
                                );

                        if (!currentPath.empty()) {
                            const auto current =
                                    agent.position();

                            const std::string title =
                                    "AURA | Energy: " + std::to_string(agent.energy()) + " | Position: (" +
                                    std::to_string(current.x) + "," + std::to_string(current.y) + ")";
                            window.setTitle(title);

                            const auto next =
                                    currentPath.front();

                            const aura::world::Position offset{
                                next.x - current.x,
                                next.y - current.y
                            };

                            static_cast<void>(
                                agent.moveBy(
                                    offset,
                                    world
                                )
                            );
                        }
                    }
                } catch (const std::exception &error) {
                    std::cerr
                            << "Invalid JSON from brain: "
                            << actionJson
                            << '\n';

                    std::cerr
                            << "Reason: "
                            << error.what()
                            << '\n';
                }
            }

            lastUpdateTime = now;
        }

        window.clear();

        GridRenderer::render(
            world,
            agent,
            rangeSensor.radius(),
            currentPath,
            hasTarget
                ? &currentTarget
                : nullptr,
            brainDebug
        );

        window.display();
    }

    return 0;
}
