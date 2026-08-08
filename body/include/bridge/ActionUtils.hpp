#pragma once

#include "bridge/Action.hpp"
#include "world/Position.hpp"

namespace aura::bridge {
    /// Converts a protocol direction into the body's one-cell grid offset.
    [[nodiscard]] world::Position directionOffset(Direction direction);
}
