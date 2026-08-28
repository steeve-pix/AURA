#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

#include "agent/Agent.hpp"
#include "bridge/Action.hpp"
#include "world/World.hpp"
#include "navigation/Pathfinder.hpp"

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
                lastMoveToTarget_.reset();
                return;
            }

            const bool sameTarget =
                    lastMoveToTarget_.has_value()
                    && *lastMoveToTarget_
                    == action.target;

            if (sameTarget) {
                return;
            }

            lastMoveToTarget_ = action.target;
            ++moveToTargetCount_;

            if (moveToTargetCount_ % interval_ != 0) {
                return;
            }

            const auto path =
                    navigation::findPath(world, agent.position(), action.target);

            if (path.empty()) {
                return;
            }

            // Obstruct the proposed route, not the
            // objective itself.
            blockedTarget_ = path.front();
            blockedCellType_ = world.cellAt(*blockedTarget_);

            world.setCell(*blockedTarget_, world::CellType::Wall);
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
        std::size_t moveToTargetCount_ = 0;
        std::optional<world::Position> lastMoveToTarget_;
        std::optional<world::Position> blockedTarget_;
        world::CellType blockedCellType_ = world::CellType::Empty;
    };
}
