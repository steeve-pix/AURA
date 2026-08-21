#pragma once

#include <vector>

#include "agent/Agent.hpp"
#include "bridge/BrainDebugState.hpp"
#include "bridge/BrainProcess.hpp"
#include "bridge/Observation.hpp"
#include "render/Window.hpp"
#include "sensors/RangeSensor.hpp"
#include "world/Position.hpp"
#include "world/World.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "scenario/Scenario.hpp"

namespace aura::app
{
    /// Owns AURA's simulation state and coordinates the body, brain, and renderer.
    ///
    /// Application advances the simulation at a fixed cadence while keeping window
    /// rendering responsive between updates. It is the top-level runtime boundary.
    class Application
    {
    public:
        /// Creates the simulation and configures the Python brain's working directory.
        Application(
            std::string brainWorkingDirectory,
            std::unique_ptr<scenario::Scenario> scenario,
            std::uint32_t mazeSeed,
            std::optional<int> maxSteps = std::nullopt
        );

        /// Launches the brain and runs until the window closes or startup fails.
        int run();

    private:
        /// Performs one observation, decision, and action cycle.
        void update();

        /// Removes route visualization and releases the current navigation target.
        void clearNavigationTarget();

        /// Validates and resolves an investigation request adjacent to AURA.
        void executeInvestigation(const bridge::Action& action);

        /// Applies one cardinal movement request.
        void executeMove(const bridge::Action& action);

        /// Advances AURA by one step along a shortest path to the requested target.
        void executeMoveTo(const bridge::Action& action);

        /// Records a successful no-op and clears active navigation state.
        void executeIdle();

        /// Captures the physical state sent to the brain for its next decision.
        [[nodiscard]] bridge::Observation buildObservation() const;

        /// Dispatches a parsed action to its body-level executor.
        void executeAction(const bridge::Action& action);

        render::Window window_;

        std::uint32_t mazeSeed_;
        std::optional<int> maxSteps_;

        world::World world_;
        agent::Agent agent_;

        sensors::RangeSensor rangeSensor_;

        bridge::BrainProcess brain_;
        /// Result reported exactly once in the observation following an action.
        std::optional<bridge::LastAction> lastAction_{};

        std::vector<world::Position> currentPath_;
        std::vector<world::CellType> investigationOutcomes_;

        world::Position currentTarget_{};
        bool hasTarget_ = false;

        /// Planning metadata used only by developer-facing rendering.
        bridge::BrainDebugState brainDebug_;

        static constexpr int NUM_BATTERIES = 12;
        static constexpr int NUM_UNKNOWN = 50;
        static constexpr int UNKNOWN_BATTERY_PERCENT = 30;
        static constexpr int INITIAL_ENERGY = 40;
        static constexpr int INITIAL_BATTERY_MAXIMUM_DISTANCE = 30;

        std::unique_ptr<scenario::Scenario> scenario_;
    };
}
