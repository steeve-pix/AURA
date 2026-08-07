#include "agent/Agent.hpp"

namespace aura::agent {
    Agent::Agent(world::Position position)
        : position_(position) {
    }

    world::Position Agent::position() const {
        return position_;
    }
}
