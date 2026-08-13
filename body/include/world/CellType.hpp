#pragma once
namespace aura::world {
    /// Mutually exclusive physical contents stored in one world cell.
    enum class CellType {
        Empty,
        Wall,
        Battery,
        Unknown
    };
}
