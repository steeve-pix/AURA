#pragma once

#include <vector>

#include "sensors/VisibleCell.hpp"
#include "sensors/VisibleObject.hpp"

namespace aura::sensors
{
    /// Snapshot collected from the square region covered by the range sensor.
    struct RangeObservation
    {
        /// Every in-bounds cell scanned, including empty cells and walls.
        std::vector<VisibleCell> cells;
        /// Batteries and unknown objects enriched with route information.
        std::vector<VisibleObject> objects;
    };
}
