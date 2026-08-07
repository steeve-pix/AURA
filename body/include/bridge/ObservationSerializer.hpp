#pragma once

#include <string>

#include "bridge/Observation.hpp"

namespace aura::bridge {
    std::string serializedObservation(const Observation &observation);
}
