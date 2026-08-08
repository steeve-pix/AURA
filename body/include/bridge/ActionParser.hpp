#pragma once

#include <string_view>

#include "bridge/Action.hpp"

namespace aura::bridge {
    [[nodiscard]] Action parseAction(std::string_view json);
}