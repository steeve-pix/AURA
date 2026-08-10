#include <iostream>
#include <ostream>
#include <GLFW/glfw3.h>

#include "bridge/Action.hpp"
#include "bridge/ActionParser.hpp"
#include "bridge/ActionUtils.hpp"
#include "bridge/BrainProcess.hpp"
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
    const std::string scriptPath = "brain.main";
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

    while (!window.shouldClose()) {
        window.pollEvents();

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
                    const auto action =
                            aura::bridge::parseAction(
                                actionJson
                            );

                    if (
                        action.type ==
                        aura::bridge::ActionType::Move
                    ) {
                        static_cast<void>(
                            agent.moveBy(
                                aura::bridge::directionOffset(
                                    action.direction
                                ),
                                world
                            )
                        );
                    }

                    if (
                        action.type ==
                        aura::bridge::ActionType::MoveTo
                    ) {
                        const auto path =
                                aura::navigation::findPath(
                                    world,
                                    agent.position(),
                                    action.target
                                );

                        if (!path.empty()) {
                            const auto current =
                                    agent.position();

                            const auto next =
                                    path.front();

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
            agent
        );

        window.display();
    }

    return 0;
}
