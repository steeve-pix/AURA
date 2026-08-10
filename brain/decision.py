"""Choose AURA's next high-level intention from a body observation."""
import random
from typing import Any
from memory import Memory


def decide(observation: dict[str, Any], goal: str, memory: Memory) -> dict[
    str, Any]:  # pyright: ignore[reportExplicitAny]
    """Move toward an observed eastern battery; otherwise remain idle."""

    if goal == "recharge":
        for obj in observation["nearby_objects"]:  # pyright: ignore[reportAny]
            if obj["type"] == "Battery":
                return {
                    "action": "move_to",
                    "target": obj["position"],
                }

        known_batteries = memory.batteries();

        if known_batteries:
            aura_x, aura_y = observation["position"]

            target = min(known_batteries, key=lambda battery: abs(battery[0] - aura_x)
                                                              + abs(battery[1] - aura_y))

            return {
                "action": "move_to",
                "target": list[target]
            }

        return {
            "action": "idle"
        }

    if goal == "explore":
        aura_x, aura_y = observation["position"]

        directions = {
            "north":(0, -1),
            "east": (1, 0),
            "south":(0, 1),
            "west": (-1, 0),
        }

        candidates = []

        for direction, (dx, dy) in directions.items():
            if observation[direction] == "Wall":
                continue

            next_position = (
                aura_x + dx,
                aura_y + dy
            )

            score = memory.visit_count(next_position)

            candidates.append((score, direction))

        if not candidates:
            return {
                "action": "idle"
            }

        lowest_score = min(score for score, _ in candidates)

        best_directions = [
            direction for score, direction in candidates if score == lowest_score
        ]

        direction = random.choice(best_directions)

        return {
            "action": "move",
            "direction": direction,
        }

    return {"action": "idle"}
