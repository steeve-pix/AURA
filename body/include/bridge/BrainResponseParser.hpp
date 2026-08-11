#pragma once

#include <string_view>

#include "bridge/BrainResponse.hpp"

namespace aura::bridge {
    BrainResponse parseBrainResponse(std::string_view text);
}