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

    void World::addBoundaryWalls() {
        if (width_ <= 0 || height_ <= 0) {
            return;
        }

        // Rewriting each corner is harmless and keeps row and column boundaries symmetric.
        for (int x = 0; x < width_; ++x) {
            setCell({x, 0}, CellType::Wall);
            setCell({x, height_ - 1}, CellType::Wall);
        }

        for (int y = 0; y < height_; ++y) {
            setCell({0, y}, CellType::Wall);
            setCell({width_ - 1, y}, CellType::Wall);
        }
    }

    bool World::canEnter(Position position) const {
        return isInside(position) && cellAt(position) != CellType::Wall;
    }

    std::size_t World::index(Position position) const {
        // Row-major storage places all cells from earlier rows before this coordinate.
        return static_cast<std::size_t>(position.y) * static_cast<std::size_t>(width_)
               + static_cast<std::size_t>(position.x);
    }
}
