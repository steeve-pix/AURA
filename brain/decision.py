"""Choose AURA's next high-level intention from a body observation."""

from typing import Any


def decide(observation: dict[str, Any], goal: str) -> dict[str, Any]:  # pyright: ignore[reportExplicitAny]
    """Move toward an observed eastern battery; otherwise remain idle."""

    if goal == "recharge":
        for obj in observation["nearby_objects"]:  # pyright: ignore[reportAny]
            if obj["type"] == "Battery":
                aura_x, aura_y = observation["position"]  # pyright: ignore[reportAny]

                battery_x, battery_y = obj["position"]  # pyright: ignore[reportAny]

                if battery_x > aura_x:
                    return {"action": "move", "direction": "east"}

                if battery_x < aura_x:
                    return {"action": "move", "direction": "west"}

                if battery_y > aura_y:
                    return {"action": "move", "direction": "south"}

                if battery_y < aura_y:
                    return {"action": "move", "direction": "north"}

            return {"action": "idle"}

    if goal == "explore":
        if observation.get("east") != "Wall":
            return {"action": "move", "direction": "east"}

    return {"action": "idle"}
