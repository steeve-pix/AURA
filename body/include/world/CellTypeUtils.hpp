#pragma once

#include <string_view>

#include "world/CellType.hpp"

namespace aura::world {
    /// Returns the stable protocol/display name for a physical cell type.
    [[nodiscard]] std::string_view toString(CellType type);
}
