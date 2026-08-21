#pragma once

#include <string_view>

#include "bridge/Action.hpp"

namespace aura::bridge {
    /// Parses and validates one JSON action emitted by the Python brain.
    ///
    /// Missing fields, malformed JSON, and unsupported action values are reported
    /// through the parsing exceptions raised by this function.
    [[nodiscard]] Action parseAction(std::string_view json);
}
