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
    Application::Application(std::string brainWorkingDirectory)
        : window_(1280, 720, "AURA"), mazeSeed_(1337), world_(42, 21), agent_({.x = 1, .y = 1}),
          rangeSensor_(3),
          brain_("python3", "-mbrain.main", std::move(brainWorkingDirectory)) {
        world::MazeGenerator generator{mazeSeed_};

        generator.generate(world_, NUM_BATTERIES, NUM_UNKNOWN);
    }

    int Application::run() {
        using render::GridRenderer;
        using render::Window;

        if (!brain_.launch()) {
            std::cerr << "Failed to launch Python brain\n";
            return 1;
        }

        double lastUpdateTime = glfwGetTime();

        constexpr double updateInterval = 0.01;


        auto formatScore = [](const bridge::BrainDebugState &debug, const std::string &name) -> std::string {
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

            std::string planLabel = "none";

            if (!debug.planGoal.empty()) {
                planLabel = debug.planGoal + " "
                        + std::to_string(debug.planCurrentStep + 1) + "/"
                        + std::to_string(debug.planStepCount);

                if (!debug.planStepType.empty()) {
                    planLabel += " " + debug.planStepType;
                }
            }

            const std::string title =
                    "AURA | Goal: " + goalLabel + " (" + goalScore + ") | Explore: " + exploreScore + " | Energy: " +
                    std::to_string(energy) + " | Plan: " + planLabel;
            window_.setTitle(title);
        };

        // Rendering remains continuous while simulation and brain updates run at a
        // fixed cadence.
        while (!window_.shouldClose()) {
            Window::pollEvents();

            const double now = glfwGetTime();

            if (now - lastUpdateTime >= updateInterval) {
                update();

                lastUpdateTime = now;
            }

            setWindowTitle(brainDebug_, agent_.energy());

            window_.clear();

            GridRenderer::render(world_, agent_, rangeSensor_.radius(), currentPath_,
                                 hasTarget_ ? &currentTarget_ : nullptr, brainDebug_
            );

            window_.display();
        }

        return 0;
    }

    void Application::update() {
        const bridge::Observation observation = buildObservation();

        const std::string observationJson =
                bridge::serializedObservation(observation);

        // The serialized observation owns this result now; later cycles must not
        // report the same outcome twice.
        lastAction_.reset();

        const std::string actionJson =
                brain_.exchange(observationJson);

        if (!actionJson.empty()) {
            try {
                const auto response =
                        bridge::parseBrainResponse(actionJson);

                brainDebug_ = response.debug;

                executeAction(response.action);
            } catch (const std::exception &error) {
                std::cerr << "Invalid JSON from brain: " << actionJson << '\n';

                std::cerr << "Reason: " << error.what() << '\n';
            }
        }
    }

    void Application::clearNavigationTarget() {
        currentPath_.clear();
        currentTarget_ = {};
        hasTarget_ = false;
    }

    void Application::executeInvestigation(const bridge::Action &action) {
        clearNavigationTarget();
        const auto agentPosition = agent_.position();
        // Manhattan distance one enforces physical, cardinal adjacency.
        const int targetDistance =
                std::abs(action.target.x - agentPosition.x) +
                std::abs(action.target.y - agentPosition.y);
        const bool validTarget =
                world_.isInside(action.target) && targetDistance == 1 &&
                world_.cellAt(action.target) ==
                world::CellType::Unknown;

        if (validTarget) {
            world_.setCell(action.target, world::CellType::Battery);

            lastAction_ = {bridge::ActionType::Investigate, action.target, true, "completed"};
        } else {
            lastAction_ = {bridge::ActionType::Investigate, action.target, false, "failed"};
        }
    }

    void Application::executeMove(const bridge::Action &action) {
        clearNavigationTarget();

        const bool moved =
                agent_.moveBy(
                    bridge::directionOffset(action.direction),
                    world_
                );

        lastAction_ = {
            bridge::ActionType::Move,
            std::nullopt,
            moved,
            moved ? "completed" : "failed"
        };
    }

    void Application::executeMoveTo(const bridge::Action &action) {
        currentTarget_ = action.target;
        hasTarget_ = true;

        currentPath_ =
                navigation::findPath(
                    world_,
                    agent_.position(),
                    currentTarget_
                );

        // Arrival is a successful no-op. An empty path to any other coordinate is
        // specifically an unreachable navigation target.
        bool moved = currentTarget_ == agent_.position();
        std::string result = moved ? "completed" : "unreachable";

        if (!currentPath_.empty()) {
            const auto current =
                    agent_.position();

            const std::string title =
                    "AURA | Energy: " + std::to_string(agent_.energy()) + " | Goal: " + brainDebug_.goal +
                    " | Position: (" + std::to_string(current.x) + "," + std::to_string(current.y) + ")";
            window_.setTitle(title);

            // MoveTo advances one path edge per update so the brain can reconsider
            // its goal after every physical step.
            const auto next =
                    currentPath_.front();

            const world::Position offset{
                next.x - current.x,
                next.y - current.y
            };

            moved = agent_.moveBy(
                offset,
                world_
            );
            result = moved ? "completed" : "failed";
        }

        lastAction_ = {
            bridge::ActionType::MoveTo,
            currentTarget_,
            moved,
            result
        };
    }

    void Application::executeIdle() {
        clearNavigationTarget();

        lastAction_ = {
            bridge::ActionType::Idle,
            std::nullopt,
            true,
            "completed"
        };
    }

    bridge::Observation Application::buildObservation() const {
        const std::string worldId =
                "maze:" + std::to_string(mazeSeed_)
                + ":" + std::to_string(world_.width())
                + "x" + std::to_string(world_.height())
                + ":b" + std::to_string(NUM_BATTERIES)
                + ":u" + std::to_string(NUM_UNKNOWN);
        const auto local =
                sensors::LocalSensor::observe(world_, agent_);

        const auto nearby =
                rangeSensor_.observe(world_, agent_);
        return {
            .position = agent_.position(),
            .energy = agent_.energy(),
            .surroundings = local,
            .nearby = nearby,
            .sensor_radius = rangeSensor_.radius(),
            .lastAction = lastAction_,
            .worldId = worldId,
        };
    }

    void Application::executeAction(const bridge::Action &action) {
        switch (action.type) {
            case bridge::ActionType::Investigate:
                executeInvestigation(action);
                break;
            case bridge::ActionType::Move:
                executeMove(action);
                break;
            case bridge::ActionType::MoveTo:
                executeMoveTo(action);
                break;
            case bridge::ActionType::Idle:
                executeIdle();
                break;
        }
    }
}
