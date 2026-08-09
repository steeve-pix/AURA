"""Choose AURA's next high-level intention from a body observation."""

from typing import Any


def decide(observation: dict[str, Any], goal: str) -> dict[str, Any]:  # pyright: ignore[reportExplicitAny]
    """Move toward an observed eastern battery; otherwise remain idle."""

    if goal == "recharge":
        for obj in observation["nearby_objects"]:  # pyright: ignore[reportAny]
            if obj["type"] == "Battery":
               return {
                   "action":"move_to",
                   "target":obj["position"],
               }

            return {"action": "idle"}

    if goal == "explore":
        if observation.get("east") != "Wall":
            return {"action": "move", "direction": "east"}

    return {"action": "idle"}
