"""Choose AURA's next high-level intention from a body observation."""

from typing import Any


def decide(observation: dict[str, Any], goal: str) -> dict[str, Any]:  # pyright: ignore[reportExplicitAny]
    """Move toward an observed eastern battery; otherwise remain idle."""

    if goal == "recharge":
         # The brain selects the goal and direction. The C++ body still validates movement.
        if observation.get("east") in ("Empty", "Battery"):
            return {"action": "move", "direction": "east"}

    if goal == "explore":
       if observation.get("east") != "Wall":
           return {
               "action": "move",
               "direction":"east"
           }

    return {"action": "idle"}
