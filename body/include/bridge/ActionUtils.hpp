#pragma once

#include "bridge/Action.hpp"
#include "world/Position.hpp"

namespace aura::bridge
{
    /// Converts a cardinal direction into a one-cell offset in world coordinates.
    ///
    /// North decreases y, east increases x, south increases y, and west decreases x.
    [[nodiscard]] world::Position directionOffset(Direction direction);
}
