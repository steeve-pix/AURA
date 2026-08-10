#pragma once

#include <string_view>

#include "bridge/Action.hpp"

namespace aura::bridge {
    /// Parses the small action JSON message used by the brain-body protocol.
    /// Throws std::invalid_argument when a required field or value is invalid.
    [[nodiscard]] Action parseAction(std::string_view json);
}
