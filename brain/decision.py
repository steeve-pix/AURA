"""Choose AURA's next high-level intention from a body observation."""
import random
from typing import Any, Union

from brain import memory
from brain.memory import Memory

BATTERY_ARRIVAL_RESERVE = 2
BATTERY_TARGET_SWITCH_MARGIN = 5


def choose_recharge_action(observation, memory):
    energy = observation["energy"]
    visible_battery_positions = {
        tuple(obj["position"])
        for obj in observation["nearby_objects"]
        if obj["type"] == "Battery"
    }

    visible_batteries = [
        obj
        for obj in observation["nearby_objects"]
        if obj["type"] == "Battery"
           and obj.get("reachable", False)
           and obj["path_length"] <= energy - BATTERY_ARRIVAL_RESERVE
           and not memory.is_failed_target(tuple(obj["position"]))
    ]

    if visible_batteries:
        best = min(
            visible_batteries,
            key=lambda obj: obj["path_length"],
        )

        active_target = memory.active_recharge_target
        active_battery = next(
            (
                obj
                for obj in visible_batteries
                if tuple(obj["position"]) == active_target
            ),
            None,
        )

        if active_battery is not None:
            improvement = (
                    active_battery["path_length"]
                    - best["path_length"]
            )

            if improvement < BATTERY_TARGET_SWITCH_MARGIN:
                best = active_battery

        target = tuple(best["position"])

        if target != memory.active_recharge_target:
            memory.set_recharge_target(target)

        return {
            "action": "move_to",
            "target": list(target),
        }

    if memory.active_recharge_target is not None:
        target = memory.active_recharge_target

        if (
                target not in visible_battery_positions
                and not memory.is_failed_target(target)
        ):
            return {
                "action": "move_to",
                "target": list(target),
            }

        memory.clear_recharge_target()

    remembered = [
        battery
        for battery in memory.batteries()
        if battery not in visible_battery_positions
           and not memory.is_failed_target(battery)
    ]

    if remembered:
        aura_x, aura_y = observation["position"]

        target = min(
            remembered,
            key=lambda battery:
            abs(battery[0] - aura_x)
            + abs(battery[1] - aura_y),
        )

        memory.set_recharge_target(target)

        return {
            "action": "move_to",
            "target": list(target),
        }

    return choose_exploration_action(observation, memory)


def decide(observation, goal, memory):
    if goal == "recharge":
        return choose_recharge_action(observation, memory)

    if goal == "explore":
        return choose_exploration_action(observation, memory)

    if goal == "investigate":
        return choose_investigation_action(observation, memory)

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


def choose_investigation_action(observation, memory: Memory):
    unknown_objects = [
        obj for obj in observation["nearby_objects"]
        if obj["type"] == "Unknown"
           and obj["reachable"]
           and not memory.is_failed_target(tuple(obj["position"]))
    ]
    if not unknown_objects:
        return {"action": "idle"}

    target = min(
        unknown_objects,
        key=lambda obj: (
            max(
                abs(obj["position"][0] - observation["position"][0]),
                abs(obj["position"][1] - observation["position"][1]),
            ),
            tuple(obj["position"]),
        ),
    )

    return {
        "action": "investigate",
        "target": target["position"]
    }
