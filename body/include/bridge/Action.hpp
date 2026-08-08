#pragma once

namespace aura::bridge {
    /// Cardinal directions understood by both the brain protocol and the body.
    enum class Direction {
        North,
        East,
        South,
        West
    };

    /// Physical action categories currently accepted from the Python brain.
    enum class ActionType {
        Idle,
        Move
    };

    /// A validated intention sent by the brain for the body to perform.
    ///
    /// direction is ignored when type is Idle.
    struct Action {
        ActionType type;
        Direction direction;
    };
}
