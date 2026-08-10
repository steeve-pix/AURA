#include "bridge/ActionUtils.hpp"

#include <stdexcept>

namespace aura::bridge {
    world::Position directionOffset(Direction direction) {
        // Screen-style coordinates place north at decreasing y.
        switch (direction) {
            case Direction::North:
                return {0, -1};
            case Direction::East:
                return {1, 0};
            case Direction::South:
                return {0, 1};
            case Direction::West:
                return {-1, 0};
        }

        throw std::invalid_argument("Unsupported direction");
    }
}
