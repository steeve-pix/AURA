#pragma once

namespace aura::world
{
    /// Integer coordinates identifying one world cell.
    ///
    /// The origin is at the upper-left: x increases eastward and y southward.
    struct Position
    {
        int x;
        int y;

        /// Compares both coordinates for exact grid-position equality.
        bool operator==(const Position& other) const
        {
            return x == other.x &&
                y == other.y;
        }
    };
}
