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

#include <optional>
#include <string>

namespace aura::app {
    class Application {
    public:
        Application(std::string brainWorkingDirectory);

        int run();

    private:
        void update();

        void clearNavigationTarget();

        void executeInvestigation(const bridge::Action &action);

        void executeMove(const bridge::Action &action);

        void executeMoveTo(const bridge::Action &action);

        void executeIdle();

        render::Window window_;

        world::World world_;
        agent::Agent agent_;

        sensors::RangeSensor rangeSensor_;

        bridge::BrainProcess brain_;
        std::optional<bridge::LastAction> lastAction_{};

        std::vector<world::Position> currentPath_;

        world::Position currentTarget_{};
        bool hasTarget_ = false;

        bridge::BrainDebugState brainDebug_;
    };
}
