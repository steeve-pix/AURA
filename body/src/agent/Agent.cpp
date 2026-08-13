#include "agent/Agent.hpp"
#include <stdexcept>

namespace aura::agent {
    Agent::Agent(world::Position position)
        : position_(position), energy_(100), maxEnergy_(100) {
        if (energy_ > maxEnergy_) {
            throw std::invalid_argument{"Energy cannot be higher than Max Energy."};
        }
    }

    world::Position Agent::position() const {
        return position_;
    }

    bool Agent::moveBy(world::Position offset, const world::World &world) {
        // The body accepts exactly one cardinal step so every successful move has
        // the same energy cost and can be represented by one pathfinding edge.
        const bool isSingleStep =
                (offset.x == 1 && offset.y == 0) ||
                (offset.x == -1 && offset.y == 0) ||
                (offset.x == 0 && offset.y == 1) ||
                (offset.x == 0 && offset.y == -1);

        if (!isSingleStep) {
            return false;
        }

        // Keep the candidate separate until every physical precondition has passed.
        world::Position next{
            .x = position_.x + offset.x,
            .y = position_.y + offset.y
        };

        if (!world.isInside(next)) {
            return false;
        }

        if (world.cellAt(next) == world::CellType::Wall) {
            return false;
        }

        if (energy_ <= 0) {
            return false;
        }

        position_ = next;
        // Energy is committed only after the position change succeeds.
        --energy_;

        interactWithCell(world);

        return true;
    }

    int Agent::energy() const {
        return energy_;
    }

    int Agent::maxEnergy() const {
        return maxEnergy_;
    }

    void Agent::interactWithCell(const world::World &world) {
        // Battery effects are tied to occupancy rather than to the brain's chosen goal.
        if (world.cellAt(position_) == world::CellType::Battery) {
            energy_ = maxEnergy_;
        }
    }
}
