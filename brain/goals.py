from typing import Any


def choose_goal(observation: dict[str, Any]):  # pyright: ignore[reportExplicitAny]
    energy = observation["energy"]  # pyright: ignore[reportAny]

    if energy < 50:
        return "recharge"

    return "explore"
