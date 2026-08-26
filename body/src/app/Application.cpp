#include "app/Application.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <vector>
#include <GLFW/glfw3.h>

#include "bridge/Action.hpp"
#include "bridge/ActionUtils.hpp"
#include "bridge/BrainProcess.hpp"
#include "bridge/BrainResponseParser.hpp"
#include "bridge/NavigationPreview.hpp"
#include "bridge/Observation.hpp"
#include "bridge/ObservationSerializer.hpp"
#include "navigation/Pathfinder.hpp"
#include "render/GridRenderer.hpp"
#include "render/Window.hpp"
#include "sensors/LocalSensor.hpp"
#include "sensors/RangeSensor.hpp"
#include "world/MazeGenerator.hpp"

namespace aura::app {
    Application::Application(
        std::string brainWorkingDirectory,
        std::unique_ptr<scenario::Scenario> scenario,
        std::uint32_t mazeSeed,
        std::optional<int> maxSteps
    )
        : window_(1280, 720, "AURA"), mazeSeed_(mazeSeed), maxSteps_(maxSteps),
          world_(42, 21), agent_({.x = 1, .y = 1}, INITIAL_ENERGY),
          rangeSensor_(3),
          brain_("./.venv/bin/python3", "-mbrain.main", std::move(brainWorkingDirectory)),
          scenario_(std::move(scenario)) {
        world::MazeGenerator generator{mazeSeed_};

        generator.generate(
            world_,
            NUM_BATTERIES,
            NUM_UNKNOWN,
            INITIAL_BATTERY_MAXIMUM_DISTANCE
        );

        int unknownCount = 0;
        for (int x = 0; x < world_.width(); ++x) {
            for (int y = 0; y < world_.height(); ++y) {
                if (world_.cellAt({x, y}) == world::CellType::Unknown) {
                    ++unknownCount;
                }
            }
        }

        const int batteryRevealCount =
                (unknownCount * UNKNOWN_BATTERY_PERCENT + 50) / 100;
        investigationOutcomes_.assign(
            batteryRevealCount,
            world::CellType::Battery
        );
        investigationOutcomes_.insert(
            investigationOutcomes_.end(),
            unknownCount - batteryRevealCount,
            world::CellType::Empty
        );

        std::mt19937 investigationRandom{mazeSeed_};
        std::shuffle(
            investigationOutcomes_.begin(),
            investigationOutcomes_.end(),
            investigationRandom
        );
    }

    int Application::run() {
        using render::GridRenderer;
        using render::Window;

        if (!brain_.launch()) {
            std::cerr << "Failed to launch Python brain\n";
            return 1;
        }

        double lastUpdateTime = glfwGetTime();
        int completedSteps = 0;

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
                    "AURA | Seed | " + std::to_string(mazeSeed_) + " | Goal: " + goalLabel + " (" + goalScore +
                    ") | Explore: " +
                    exploreScore + " | Energy: " +
                    std::to_string(energy) + " | Plan: " + planLabel
                    + " | Failures P/R/T/B: "
                    + std::to_string(debug.planFailures) + "/"
                    + std::to_string(debug.replans) + "/"
                    + std::to_string(debug.failedTargets) + "/"
                    + std::to_string(debug.bodyActionFailures);
            window_.setTitle(title);
        };

        // Rendering remains continuous while simulation and brain updates run at a
        // fixed cadence.
        while (
            !window_.shouldClose()
            && (!maxSteps_.has_value() || completedSteps < *maxSteps_)
        ) {
            Window::pollEvents();

            const double now = glfwGetTime();

            if (now - lastUpdateTime >= updateInterval) {
                update();
                ++completedSteps;

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

        std::string responseJson =
                brain_.exchange(observationJson);

        if (!responseJson.empty()) {
            try {
                auto response =
                        bridge::parseBrainResponse(responseJson);

                if (response.type == bridge::BrainResponseType::PreviewRequest) {
                    const auto previews = bridge::previewNavigation(
                        world_,
                        agent_.position(),
                        response.previewCandidates
                    );
                    const auto previewJson =
                            bridge::serializedNavigationPreviewResponse(previews);

                    responseJson = brain_.exchange(previewJson);

                    if (responseJson.empty()) {
                        return;
                    }

                    response = bridge::parseBrainResponse(responseJson);

                    if (response.type != bridge::BrainResponseType::Action) {
                        throw std::invalid_argument(
                            "Expected an action after navigation preview"
                        );
                    }
                }

                brainDebug_ = response.debug;

                scenario_->beforeAction(world_, agent_, response.action);

                executeAction(response.action);

                scenario_->afterAction(world_, agent_, response.action);
            } catch (const std::exception &error) {
                std::cerr << "Invalid JSON from brain: " << responseJson << '\n';

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
            const auto revealedType = investigationOutcomes_.back();
            investigationOutcomes_.pop_back();

            world_.setCell(action.target, revealedType);

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
            .type = bridge::ActionType::Move,
            .target = std::nullopt,
            .succeeded = moved,
            .result = moved ? "completed" : "failed"
        };
    }

    void Application::executeMoveTo(const bridge::Action &action) {
        currentTarget_ = action.target;
        hasTarget_ = true;

        const auto pathBefore =
                navigation::findPath(
                    world_,
                    agent_.position(),
                    action.target
                );

        currentPath_ = pathBefore;

        const int pathLengthBefore =
                agent_.position() == action.target
                    ? 0
                    : static_cast<int>(pathBefore.size());

        const std::optional<world::Position> nextStepBefore =
                pathBefore.empty()
                    ? std::nullopt
                    : std::optional{pathBefore.front()};

        if (pathBefore.empty() && agent_.position() != action.target) {
            lastAction_ = {
                .type = action.type,
                .target = action.target,
                .succeeded = false,
                .result = "unreachable",
                .pathLengthBefore = std::nullopt,
                .pathLengthAfter = std::nullopt,
                .nextStepBefore = std::nullopt,
                .nextStepAfter = std::nullopt,
                .reachableBefore = false
            };
            return;
        }

        // Arrival is a successful no-op.
        bool moved = agent_.position() == action.target;

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
        }

        const auto pathAfter =
                navigation::findPath(
                    world_,
                    agent_.position(),
                    action.target
                );

        const int pathLengthAfter =
                agent_.position() == action.target
                    ? 0
                    : static_cast<int>(pathAfter.size());

        const std::optional<world::Position> nextStepAfter =
                pathAfter.empty()
                    ? std::nullopt
                    : std::optional{pathAfter.front()};

        lastAction_ = {
            .type = action.type,
            .target = action.target,
            .succeeded = moved,
            .result = moved ? "completed" : "failed",
            .pathLengthBefore = pathLengthBefore,
            .pathLengthAfter = pathLengthAfter,
            .nextStepBefore = nextStepBefore,
            .nextStepAfter = nextStepAfter,
            .reachableBefore = true
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
                + ":u" + std::to_string(NUM_UNKNOWN)
                + ":e" + std::to_string(INITIAL_ENERGY)
                + ":ib" + std::to_string(UNKNOWN_BATTERY_PERCENT)
                + ":bd" + std::to_string(INITIAL_BATTERY_MAXIMUM_DISTANCE)
                + ":s" + scenario_->id();
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
