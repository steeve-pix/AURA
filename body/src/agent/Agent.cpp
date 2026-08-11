#include "agent/Agent.hpp"
#include <stdexcept>

namespace aura::agent {
    Agent::Agent(world::Position position)
        : position_(position), energy_(51), maxEnergy_(100) {
        if (energy_ > maxEnergy_) {
            throw std::invalid_argument{"Energy cannot be higher than Max Energy."};
        }
    }

    world::Position Agent::position() const {
        return position_;
    }

    bool Agent::moveBy(world::Position offset, const world::World &world) {
        // Diagonal moves, multi-cell jumps, and staying still are not physical steps.
        const bool isSingleStep =
                (offset.x == 1 && offset.y == 0) ||
                (offset.x == -1 && offset.y == 0) ||
                (offset.x == 0 && offset.y == 1) ||
                (offset.x == 0 && offset.y == -1);

        if (!isSingleStep) {
            return false;
        }

        // Calculate first so a rejected move never changes the agent's real position.
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
        // Only completed movement consumes energy.
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
        // Recharging is a physical consequence of occupying a battery cell.
        if (world.cellAt(position_) == world::CellType::Battery) {
            energy_ = maxEnergy_;
        }
    }
}
