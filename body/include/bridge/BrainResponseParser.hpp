#pragma once

#include <string_view>

#include "bridge/BrainResponse.hpp"

namespace aura::bridge {
    /// Parses one brain response containing an action and optional debug metadata.
    ///
    /// Invalid JSON or malformed action fields are propagated as parsing exceptions.
    BrainResponse parseBrainResponse(std::string_view text);
}
