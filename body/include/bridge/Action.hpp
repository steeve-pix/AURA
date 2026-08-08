#pragma once

namespace aura::bridge {
    enum class Direction {
        North,
        East,
        South,
        West
    };

    enum class ActionType {
        Idle,
        Move
    };

    struct Action {
        ActionType type;
        Direction direction;
    };
}