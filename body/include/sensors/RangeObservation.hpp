#pragma once

#include <vector>

#include "sensors/VisibleCell.hpp"
#include "sensors/VisibleObject.hpp"

namespace aura::sensors {
    struct RangeObservation {
        std::vector<VisibleCell> cells;
        std::vector<VisibleObject> objects;
    };
}
