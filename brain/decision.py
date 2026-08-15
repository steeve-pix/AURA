"""Choose AURA's next high-level intention from a body observation."""
import random
from typing import Any, Union

from brain.memory import Memory
from brain.planning import Plan, PlanStep, create_recharge_plan

BATTERY_ARRIVAL_RESERVE = 2


def remembered_battery_score(memory: Memory, battery: tuple[int, int], aura_position: tuple[int, int], ) -> float:
    trust = memory.battery_trust(battery)
    distance = (
            abs(battery[0] - aura_position[0])
            + abs(battery[1] - aura_position[1])
    )

    return trust / (1.0 + distance)


def choose_recharge_action(observation, memory):
    if (
            memory.active_plan is not None
            and memory.active_plan.goal == "recharge"
    ):
        return action_from_plan(memory.active_plan)

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

        target = tuple(best["position"])
        plan = create_recharge_plan(target)
        memory.set_active_plan(plan)

        return action_from_plan(plan)

    remembered = [
        battery
        for battery in memory.batteries()
        if battery not in visible_battery_positions
           and not memory.is_failed_target(battery)
    ]

    if remembered:
        x, y = observation["position"]
        aura_position = (x, y)

        target = max(
            remembered,
            key=lambda battery: remembered_battery_score(
                memory,
                battery,
                aura_position,
            ),
        )

        plan = create_recharge_plan(target)
        memory.set_active_plan(plan)

        return action_from_plan(plan)

    return choose_exploration_action(observation, memory)


def decide(observation, goal, memory):
    if (
            memory.active_plan is not None
            and memory.active_plan.goal != goal
    ):
        memory.clear_active_plan()

    if (
            memory.active_plan is not None
            and memory.active_plan.goal == goal
    ):
        return action_from_plan(memory.active_plan)

    if goal == "recharge":
        return choose_recharge_action(observation, memory)

    if goal == "explore":
        return choose_exploration_action(observation, memory)

    if goal == "investigate":
        return choose_investigation_action(observation, memory)

    return {"action": "idle"}


def action_from_plan(plan: Plan) -> dict:
    step = plan.current_step()

    if step is None or step.target is None:
        return {"action": "idle"}

    if step.step_type == "move_to":
        return {
            "action": "move_to",
            "target": list(step.target),
        }

    if step.step_type == "investigate":
        return {
            "action": "investigate",
            "target": list(step.target),
        }

    return {"action": "idle"}


def choose_exploration_action(observation, memory: Memory):
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

    # Randomness applies only among equally least-visited legal directions.
    lowest_score = min(score for score, _ in candidates)
    best_directions = [
        dir_name for score, dir_name in candidates if score == lowest_score
    ]

    return {
        "action": "move",
        "direction": random.choice(best_directions),
    }


def choose_investigation_action(observation, memory: Memory):
    if (
            memory.active_plan is not None
            and memory.active_plan.goal == "investigate"
    ):
        return action_from_plan(memory.active_plan)

    unknown_objects = [
        obj for obj in observation["nearby_objects"]
        if obj["type"] == "Unknown" and obj["reachable"]
           and not memory.is_failed_target((int(obj["position"][0]), int(obj["position"][1])))
    ]
    if not unknown_objects:
        return {"action": "idle"}

    target = max(
        unknown_objects,
        key=lambda obj: (
            investigation_target_score(obj, observation, memory),
            tuple(obj["position"]),
        ),
    )

    target_position = tuple(target["position"])

    plan = create_investigation_plan(
        observation,
        memory,
        target_position,
    )

    if plan is None:
        return {"action": "idle"}

    memory.set_active_plan(plan)
    return action_from_plan(plan)


def create_investigation_plan(
        observation,
        memory: Memory,
        target_position: tuple[int, int],
) -> Plan | None:
    target_object = next(
        (
            obj
            for obj in observation["nearby_objects"]
            if obj["type"] == "Unknown"
               and tuple(obj["position"]) == target_position
        ),
        None,
    )

    if (
            target_object is None
            or not target_object.get("reachable", False)
            or memory.is_failed_target(target_position)
    ):
        return None

    aura_position = tuple(observation["position"])
    target_x, target_y = target_position

    # Investigation is physical: AURA must share a cardinal edge with the object.
    if abs(target_x - aura_position[0]) + abs(target_y - aura_position[1]) == 1:
        plan = Plan(
            goal="investigate",
            goal_target=target_position,
            steps=[
                PlanStep(
                    step_type="investigate",
                    target=target_position,
                ),
            ],
        )
        return plan

    visible_cells = {
        tuple(cell["position"]): cell["type"]
        for cell in observation["visible_cells"]
    }
    adjacent_positions = [
        (target_x, target_y - 1),
        (target_x + 1, target_y),
        (target_x, target_y + 1),
        (target_x - 1, target_y),
    ]
    # Unknown cells are not valid staging positions; AURA approaches from a known,
    # walkable neighbor and remembers that choice to prevent left-right oscillation.
    approach_candidates = [
        position for position in adjacent_positions
        if visible_cells.get(position) not in {None, "Wall", "Unknown"}
           and not memory.is_failed_target(position)
    ]

    # Reject the object itself only after every visible adjacent approach is unusable.
    if not approach_candidates:
        memory.mark_target_failed(target_position)
        return None

    approach = min(
        approach_candidates,
        key=lambda position: (
            abs(position[0] - aura_position[0])
            + abs(position[1] - aura_position[1]),
            memory.visit_count(position),
            position,
        ),
    )

    plan = Plan(
        goal="investigate",
        goal_target=target_position,
        steps=[
            PlanStep(
                step_type="move_to",
                target=approach,
                requires_reachable_target=True,
            ),
            PlanStep(step_type="investigate", target=target_position, ),
        ],
    )
    return plan


def replan_failed_investigation(
        observation,
        memory: Memory,
) -> bool:
    failed_plan = memory.active_plan

    if (
            failed_plan is None
            or failed_plan.goal != "investigate"
            or not failed_plan.has_failed()
    ):
        return False

    target_position = failed_plan.goal_target
    memory.clear_active_plan()

    if target_position is None:
        return False

    replacement = create_investigation_plan(
        observation,
        memory,
        target_position,
    )

    if replacement is None:
        return False

    memory.set_active_plan(replacement)
    return True


HISTORICAL_BATTERY_BONUS = 0.15


def investigation_target_score(obj, observation, memory: Memory) -> float:
    aura_x, aura_y = observation["position"]
    target_x, target_y = obj["position"]

    distance = (abs(target_x - aura_x) + abs(target_y - aura_y))

    distance_score = 1.0 / (1.0 + distance)

    previous_result = (memory.previous_investigation_result(obj["position"]))

    history_bonus = 0.0

    if previous_result == "Battery":
        history_bonus = HISTORICAL_BATTERY_BONUS

    return distance_score + history_bonus
