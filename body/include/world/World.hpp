#pragma once

#include <vector>
#include <cstddef>

#include "CellType.hpp"
#include "Position.hpp"

namespace aura::world
{
    /// Owns the rectangular grid and provides bounds-safe spatial queries.
    ///
    /// World stores physical state only. Planning, sensing, and rendering are handled
    /// by separate components that consume this state.
    class World
    {
    public:
        /// Allocates a `width` by `height` grid initialized with empty cells.
        World(int width, int height);

        /// Returns the number of columns.
        [[nodiscard]] int width() const;

        /// Returns the number of rows.
        [[nodiscard]] int height() const;

        /// Reports whether both coordinates lie inside the allocated grid.
        [[nodiscard]] bool isInside(Position position) const;

        /// Returns the content at a valid position.
        [[nodiscard]] CellType cellAt(Position position) const;

        /// Replaces the content at a valid position.
        void setCell(Position position, CellType type);

        /// Converts every cell on the outer border to a wall.
        void addBoundaryWalls();

        /// Reports whether a position is in bounds and not occupied by a wall.
        [[nodiscard]] bool canEnter(Position position) const;

    private:
        int width_;
        int height_;
        std::vector<CellType> cells_;

        /// Maps valid 2D coordinates to the row-major storage index.
        [[nodiscard]] std::size_t index(Position position) const;
    };
}
