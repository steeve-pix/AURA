#pragma once

#include <vector>
#include <cstddef>

#include "CellType.hpp"
#include "Position.hpp"

namespace aura::world {
    /// Owns the rectangular grid and its physical cell contents.
    ///
    /// World performs no high-level reasoning; it only answers body-level spatial
    /// questions and applies explicit cell changes.
    class World {
    public:
        /// Creates an empty grid with the requested dimensions.
        World(int width, int height);

        /// Returns the number of grid columns.
        [[nodiscard]] int width() const;

        /// Returns the number of grid rows.
        [[nodiscard]] int height() const;

        /// Reports whether a position lies within the allocated grid.
        [[nodiscard]] bool isInside(Position position) const;

        /// Returns the contents of a valid position.
        [[nodiscard]] CellType cellAt(Position position) const;

        /// Replaces the contents of a valid position.
        void setCell(Position position, CellType type);

        /// Fills the outermost rows and columns with wall cells.
        void addBoundaryWalls();

        /// Reports whether a position is inside the grid and not a wall.
        [[nodiscard]] bool canEnter(Position position) const;

    private:
        int width_;
        int height_;
        std::vector<CellType> cells_;

        /// Converts 2D coordinates into the flat vector index used for storage.
        [[nodiscard]] std::size_t index(Position position) const;
    };
}
