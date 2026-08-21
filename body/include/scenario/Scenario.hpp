#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

#include "agent/Agent.hpp"
#include "bridge/Action.hpp"
#include "world/World.hpp"

namespace aura::scenario {
    class Scenario {
    public:
        virtual ~Scenario() = default;

        [[nodiscard]] virtual std::string id() const {
            return "normal";
        }

        virtual void beforeAction(world::World &world, const agent::Agent &agent, const bridge::Action &action) {
        }

        virtual void afterAction(world::World &world, const agent::Agent &agent, const bridge::Action &action) {
        }
    };

    class PeriodicMoveToBlock final : public Scenario {
    public:
        explicit PeriodicMoveToBlock(std::size_t interval)
            : interval_(interval) {
            if (interval_ == 0) {
                throw std::invalid_argument{"Scenario interval must be positive."};
            }
        }

        [[nodiscard]] std::string id() const override {
            return "challenge" + std::to_string(interval_);
        }

        void beforeAction(world::World &world, const agent::Agent &agent, const bridge::Action &action) override {
            if (action.type != bridge::ActionType::MoveTo) {
                return;
            }

            ++moveToCount_;

            if (
                moveToCount_ % interval_ != 0
                || action.target == agent.position()
                || !world.canEnter(action.target)
            ) {
                return;
            }

            blockedTarget_ = action.target;
            blockedCellType_ = world.cellAt(action.target);
            world.setCell(action.target, world::CellType::Wall);
        }

        void afterAction(world::World &world, const agent::Agent &agent, const bridge::Action &action) override {
            if (!blockedTarget_.has_value()) {
                return;
            }

            world.setCell(*blockedTarget_, blockedCellType_);
            blockedTarget_.reset();
        }

    private:
        std::size_t interval_;
        std::size_t moveToCount_ = 0;
        std::optional<world::Position> blockedTarget_;
        world::CellType blockedCellType_ = world::CellType::Empty;
    };
}
