#include "world/World.hpp"

namespace aura::world {
    World::World(int width, int height)
        : width_(width), height_(height), cells_(height * width, CellType::Empty) {
    }

    int World::width() const {
        return width_;
    }

    int World::height() const {
        return height_;
    }

    bool World::isInside(Position position) const {
        return position.x >= 0 &&
            position.x < width_ &&
                position.y >= 0 &&
            position.y < height_;
    }

    CellType World::cellAt(Position position) const {
        return cells_[index(position)];
    }

    void World::setCell(Position position, CellType type) {
        cells_[index(position)] = type;
    }

    std::size_t World::index(Position position) const {
        return static_cast<std::size_t>(position.y * width_ + position.x);
    }
}
