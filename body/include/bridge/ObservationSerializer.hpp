#pragma once

#include <string>

#include "bridge/Observation.hpp"

namespace aura::bridge
{
    /// Serializes an observation using the stable field names expected by the brain.
    std::string serializedObservation(const Observation& observation);
}
