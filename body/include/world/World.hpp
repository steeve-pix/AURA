#pragma once

#include <vector>
#include <cstddef>

#include "CellType.hpp"
#include "Position.hpp"

namespace aura::world {
    class World {
    public:
        World(int width, int height);

        [[nodiscard]] int width() const;

        [[nodiscard]] int height() const;

        [[nodiscard]] bool isInside(Position position) const;

        [[nodiscard]] CellType cellAt(Position position) const;

        void setCell(Position position, CellType type);

    private:
        int width_;
        int height_;
        std::vector<CellType> cells_;

        [[nodiscard]] std::size_t index(Position position) const;
    };
}
