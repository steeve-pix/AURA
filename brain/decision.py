"""Choose AURA's next high-level intention from a body observation."""
import random
from typing import Any, Union
from brain.memory import Memory


def decide(observation: dict[str, Any], goal: str, memory: Memory) -> dict[
    str, Any]:  # pyright: ignore[reportExplicitAny]
    """Choose a high-level action that serves the current goal."""

    if goal == "recharge":
        aura_x, aura_y = observation["position"]

        for obj in observation["nearby_objects"]:  # pyright: ignore[reportAny]
            if obj["type"] != "Battery":
                continue

            target = tuple(obj["position"])

            if memory.failed_target_count(target) < 2:
                return {
                    "action": "move_to",
                    "target": list(target)
                }

        usable_batteries = [
            battery
            for battery in memory.batteries()
            if memory.failed_target_count(battery) < 2
        ]

        if usable_batteries:
            target = min(
                usable_batteries,
                key=lambda battery: abs(battery[0] - aura_x) + abs(battery[1] - aura_y),
            )

            return {
                "action": "move_to",
                "target": list(target)
            }

        return choose_exploration_action(observation, memory)

    if goal == "explore":
        return choose_exploration_action(observation, memory)

    return {"action": "idle"}


def choose_exploration_action(observation, memory: Memory) -> Union[None, dict[str, Any], dict[str, Union[str, Any]]]:
    """Choose a high-level action that serves the current goal."""
    aura_x, aura_y = observation["position"]

    directions = {
        "north": (0, -1),
        "east": (1, 0),
        "south": (0, 1),
        "west": (-1, 0),
    }

    candidates = []

    for dir_name, (dx, dy) in directions.items():
        if observation.get(dir_name) == "Wall":
            continue

        next_position = (
            aura_x + dx,
            aura_y + dy
        )

        score = memory.visit_count(next_position)

        candidates.append((score, dir_name))

    if not candidates:
        return {"action": "idle"}

    lowest_score = min(score for score, _ in candidates)

    best_directions = [
        dir_name for score, dir_name in candidates if score == lowest_score
    ]

    return {
        "action": "move",
        "direction": random.choice(best_directions),
    }
