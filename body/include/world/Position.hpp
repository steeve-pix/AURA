#pragma once

namespace aura::world {
    /// Integer coordinates for one cell in the 2D world.
    ///
    /// x increases toward the east and y increases toward the south.
    struct Position {
        int x;
        int y;

        bool operator==(const Position &other) const {
            return x == other.x &&
                   y == other.y;
        }
    };
}
