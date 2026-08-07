#include "agent/Agent.hpp"

namespace aura::agent {
    Agent::Agent(world::Position position)
        : position_(position), energy_(100), maxEnergy_(100) {
    }

    world::Position Agent::position() const {
        return position_;
    }

    bool Agent::moveBy(world::Position offset, const world::World &world) {
        const bool isSingleStep =
                (offset.x == 1 && offset.y == 0) ||
                (offset.x == -1 && offset.y == 0) ||
                (offset.x == 0 && offset.y == 1) ||
                (offset.x == 0 && offset.y == -1);

        if (!isSingleStep) {
            return false;
        }

        world::Position next{
            .x = position_.x + offset.x,
            .y = position_.y + offset.y
        };

        if (!world.isInside(next)) {
            return false;
        }

        if (world.cellAt(next) == CellType::Wall) {
            return false;
        }

        if (energy_ <= 0) {
            return false;
        }

        position_ = next;
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
        if (world.cellAt(position_) == CellType::Battery) {
            energy_ = maxEnergy_;
        }
    }
}
