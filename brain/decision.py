"""Choose AURA's next high-level intention from a body observation."""
import random
from typing import Any, Union
from brain.memory import Memory


def choose_recharge_action(observation, memory):
    # 1) Keep existing target only if still valid.
    if memory.active_recharge_target is not None:
        target = memory.active_recharge_target
        if not memory.is_failed_target(target):
            return {"action": "move_to", "target": list(target)}
        memory.clear_recharge_target()

    # 2) Re-rank to visible reachable batteries first (shortest path).
    visible_batteries = [
        obj
        for obj in observation["nearby_objects"]
        if obj["type"] == "Battery"
           and obj.get("reachable", False)
           and not memory.is_failed_target(tuple(obj["position"]))
    ]

    if visible_batteries:
        best = min(visible_batteries, key=lambda obj: obj["path_length"])
        target = tuple(best["position"])

        if not memory.is_failed_target(target):
            memory.set_recharge_target(target)
            return {"action": "move_to", "target": list(target)}

        if memory.active_recharge_target == target:
            memory.clear_recharge_target()

    # 3) Fallback to remembered usable batteries.
    remembered = [
        battery for battery in memory.batteries() if not memory.is_failed_target(battery)
    ]
    if remembered:
        target = remembered[0]

        memory.set_recharge_target(target)

        return {"action": "move_to", "target": list(target)}

    return choose_exploration_action(observation, memory)


def decide(observation, goal, memory):
    if goal == "recharge":
        return choose_recharge_action(observation, memory)

    if goal == "explore":
        return choose_exploration_action(observation, memory)

    return {"action": "idle"}


def choose_exploration_action(
        observation,
        memory: Memory
) -> Union[None, dict[str, Any], dict[str, Union[str, Any]]]:
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
