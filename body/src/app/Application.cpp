#include "app/Application.hpp"

#include <iostream>
#include <optional>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <GLFW/glfw3.h>

#include "bridge/Action.hpp"
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
#include "world/MazeGenerator.hpp"

namespace aura::app {
    int Application::run() {
        using render::GridRenderer;
        using render::Window;

        Window window{1280, 720, "AURA"};

        constexpr int WIDTH = 41;
        constexpr int HEIGHT = 21;
        constexpr int NUM_BATTERIES = 12;
        constexpr int NUM_UNKNOWN = 20;

        world::World world{WIDTH, HEIGHT};

        world::MazeGenerator generator{1337};
        generator.generate(world, NUM_BATTERIES, NUM_UNKNOWN);

        // Agent starts safely at guaranteed open position {1, 1}
        agent::Agent agent{{1, 1}};

        sensors::RangeSensor rangeSensor{3};

#if defined(_WIN32)
        const std::string pythonExecutable = "python";
        const std::string scriptPath = "-m brain.main";
        const std::string workingDirectory =
                R"(C:\Users\Steeve Dim\Documents\AURA)";
#else
        const std::string pythonExecutable = "python3";
        const std::string scriptPath = "-mbrain.main";
        const std::string workingDirectory =
                "/Users/steeve.dimitry/Developer/AURA";
#endif

        bridge::BrainProcess brain{
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

        std::vector<world::Position> currentPath;

        world::Position currentTarget{};
        bool hasTarget = false;

        bridge::BrainDebugState brainDebug;

        std::optional<bridge::LastAction> lastAction;

        auto formatScore = [](const bridge::BrainDebugState &debug, const std::string &name) {
            const auto it = debug.goalScores.find(name);

            if (it == debug.goalScores.end()) {
                return std::string{"n/a"};
            }

            std::ostringstream score;
            score << std::fixed << std::setprecision(2) << it->second;
            return score.str();
        };

        auto setWindowTitle = [&](const bridge::BrainDebugState &debug, int energy) {
            const std::string goalLabel = debug.goal.empty() ? "unknown" : debug.goal;
            const std::string goalScore = formatScore(debug, debug.goal.empty() ? std::string{} : debug.goal);
            const std::string exploreScore = formatScore(debug, "explore");

            const std::string title =
                    "AURA | Goal: " + goalLabel + " (" + goalScore + ") | Explore: " + exploreScore + " | Energy: " +
                    std::to_string(energy);
            window.setTitle(title);
        };

        while (!window.shouldClose()) {
            Window::pollEvents();

            const double now = glfwGetTime();

            if (now - lastUpdateTime >= updateInterval) {
                const auto local =
                        sensors::LocalSensor::observe(
                            world,
                            agent
                        );

                const auto nearby =
                        rangeSensor.observe(
                            world,
                            agent
                        );

                const bridge::Observation observation{
                    agent.position(),
                    agent.energy(),
                    local,
                    nearby,
                    rangeSensor.radius(),
                    lastAction
                };

                const std::string observationJson =
                        bridge::serializedObservation(
                            observation
                        );

                lastAction.reset();

                const std::string actionJson =
                        brain.exchange(
                            observationJson
                        );

                if (!actionJson.empty()) {
                    try {
                        const auto response =
                                bridge::parseBrainResponse(actionJson);

                        brainDebug = response.debug;

                        const auto action =
                                response.action;

                        if (action.type == bridge::ActionType::Investigate) {
                            currentPath.clear();
                            hasTarget = false;

                            const auto agentPosition = agent.position();
                            const int targetDistance =
                                    std::abs(action.target.x - agentPosition.x) +
                                    std::abs(action.target.y - agentPosition.y);
                            const bool validTarget =
                                    world.isInside(action.target) && targetDistance == 1 &&
                                    world.cellAt(action.target) ==
                                    world::CellType::Unknown;

                            if (validTarget) {
                                world.setCell(action.target, world::CellType::Battery);

                                lastAction = {bridge::ActionType::Investigate, action.target, true};
                            } else {
                                lastAction = {bridge::ActionType::Investigate, action.target, false};
                            }
                        }
                        if (action.type == bridge::ActionType::Move) {
                            currentPath.clear();
                            hasTarget = false;

                            const bool moved =
                                    agent.moveBy(
                                        bridge::directionOffset(action.direction),
                                        world
                                    );

                            lastAction = {
                                bridge::ActionType::Move,
                                std::nullopt,
                                moved
                            };
                        }

                        if (action.type == bridge::ActionType::MoveTo) {
                            std::cout
                                    << "Target: ("
                                    << action.target.x
                                    << ", "
                                    << action.target.y
                                    << ")"
                                    << '\n';

                            currentTarget = action.target;
                            hasTarget = true;

                            currentPath =
                                    navigation::findPath(
                                        world,
                                        agent.position(),
                                        currentTarget
                                    );

                            bool moved = currentTarget == agent.position();

                            if (!currentPath.empty()) {
                                const auto current =
                                        agent.position();

                                const std::string title =
                                        "AURA | Energy: " + std::to_string(agent.energy()) + " | Goal: " + brainDebug.
                                        goal +
                                        " | Position: (" +
                                        std::to_string(current.x) + "," + std::to_string(current.y) + ")";
                                window.setTitle(title);

                                const auto next =
                                        currentPath.front();

                                const world::Position offset{
                                    next.x - current.x,
                                    next.y - current.y
                                };

                                moved = agent.moveBy(
                                    offset,
                                    world
                                );
                            } else if (!moved) {
                                std::cout << "No path to target\n";
                            }

                            lastAction = {
                                bridge::ActionType::MoveTo,
                                currentTarget,
                                moved
                            };
                        }

                        if (action.type == bridge::ActionType::Idle) {
                            currentPath.clear();
                            hasTarget = false;

                            lastAction = {
                                bridge::ActionType::Idle,
                                std::nullopt,
                                true
                            };
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

            setWindowTitle(brainDebug, static_cast<int>(agent.energy()));

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
}
