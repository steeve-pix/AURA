#include "agent/Agent.hpp"

aura::agent::Agent::Agent(world::Position position)
    : position_(position) {
}

aura::world::Position aura::agent::Agent::position() const {
    return position_;
}
