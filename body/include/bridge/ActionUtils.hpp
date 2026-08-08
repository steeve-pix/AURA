#pragma once

#include "bridge/Action.hpp"
#include "world/Position.hpp"

namespace aura::bridge {
    [[nodiscard]] world::Position directionOffset(Direction direction);
}
