"""Choose AURA's next high-level intention from a body observation."""
from typing import Any
from brain.memory import Memory


def decide(observation: dict[str, Any], goal: str, memory: Memory) -> dict[
    str, Any]:  # pyright: ignore[reportExplicitAny]
    """Choose a high-level action that serves the current goal."""

    if goal == "recharge":
        for obj in observation["nearby_objects"]:  # pyright: ignore[reportAny]
            if obj["type"] == "Battery":
                return {
                    "action": "move_to",
                    "target": obj["position"],
                }

        known_batteries = memory.batteries()

        if known_batteries:
            aura_x, aura_y = observation["position"]

            target = min(known_batteries, key=lambda battery: abs(battery[0] - aura_x)
                                                              + abs(battery[1] - aura_y))

            return {
                "action": "move_to",
                "target": list(target)
            }

        return {
            "action": "idle"
        }

    if goal == "explore":
        target = memory.least_visited_position()

        if target is None:
            return {
                "action": "idle"
            }

        return {
            "action": "move_to",
            "target": list(target),
        }

    return {"action": "idle"}
