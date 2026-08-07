#include "agent/Agent.hpp"

namespace aura::agent {
    Agent::Agent(world::Position position)
        : position_(position) {
    }

    world::Position Agent::position() const {
        return position_;
    }

    bool Agent::moveTo(world::Position position, const world::World& world) {
        if (!world.isInside(position)) {
            return false;
        }

        if (world.cellAt(position) == CellType::Wall) {
            return false;
        }

        position_ = position;
        return true;
    }
}
