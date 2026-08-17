#pragma once

#include "agent/Agent.hpp"
#include "bridge/Action.hpp"
#include "world/World.hpp"

namespace aura::scenario {
    class Scenario {
    public:
        virtual ~Scenario() = default;

        virtual void beforeAction(world::World &world, const agent::Agent &agent, const bridge::Action &action) {
        }
    };

    class BlockFirstMoveTo final : public Scenario {
    public:
        void beforeAction(world::World &world, const agent::Agent &agent, const bridge::Action &action) override {
            if (triggered_ || action.type != bridge::ActionType::MoveTo) {
                return;
            }

            world.setCell(action.target, world::CellType::Wall);

            triggered_ = true;
        }

    private:
        bool triggered_ = false;
    };
}
