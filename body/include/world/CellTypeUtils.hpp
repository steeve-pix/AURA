#pragma once

#include <string_view>

#include "world/CellType.hpp"

namespace aura::world
{
    /// Returns the stable, case-sensitive name used by JSON and renderers.
    [[nodiscard]] std::string_view toString(CellType type);
}
