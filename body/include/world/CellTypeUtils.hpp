#pragma once

#include <string_view>

#include "world/CellType.hpp"

namespace aura::world {
    [[nodiscard]] std::string_view toString(CellType type);
}
