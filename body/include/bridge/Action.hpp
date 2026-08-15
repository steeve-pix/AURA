#pragma once

#include "world/Position.hpp"

namespace aura::bridge {
    /// Cardinal directions accepted by the brain-to-body protocol.
    enum class Direction {
        North,
        East,
        South,
        West
    };

    /// Operations the C++ body can execute on behalf of the Python brain.
    enum class ActionType {
        Idle,
        Move,
        MoveTo,
        Investigate
    };

    /// One parsed command from the Python brain.
    ///
    /// `direction` is meaningful only for Move. `target` is meaningful only for
    /// MoveTo and Investigate; the parser supplies placeholders for unused fields.
    struct Action {
        ActionType type;
        Direction direction;
        world::Position target;
    };
}
