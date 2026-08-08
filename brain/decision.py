"""Choose AURA's next high-level intention from a body observation."""

from typing import Any


def decide(observation: dict[str, Any]) -> dict[str, Any]:
    """Move toward an observed eastern battery; otherwise remain idle."""

    # The brain selects the goal and direction. The C++ body still validates movement.
    if observation["east"] == "Battery":
        return {
            "action": "move",
            "direction": "east"
        }
        
    return {
        "action": "idle"
    }
