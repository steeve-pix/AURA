#pragma once

#include <string>

#include "bridge/Observation.hpp"

namespace aura::bridge {
    /// Converts a body observation into the JSON representation consumed by Python.
    std::string serializedObservation(const Observation &observation);
}
