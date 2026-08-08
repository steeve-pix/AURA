#pragma once

#include <vector>

#include "sensors/VisibleObject.hpp"

namespace aura::sensors {
    struct RangeObservation {
        std::vector<VisibleObject> objects;
    };
}