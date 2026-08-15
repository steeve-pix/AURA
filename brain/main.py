import json
import sys
from typing import Any
from pathlib import Path

from brain.decision import decide
from brain.goals import choose_goal, goal_scores
from brain.memory_store import load_memory, memory_path_for_world, save_memory
from brain.planning import update_plan_from_observation


def main() -> None:
    memory_directory = Path("data")

    memory = None
    active_world_id = None
    memory_path = None

    for raw in sys.stdin:
        raw = raw.strip()

        if not raw:
            continue

        observation: dict[str, Any] = json.loads(raw)

        world_id = observation["world_id"]

        if memory is None or world_id != active_world_id:
            memory_path = memory_path_for_world(memory_directory, world_id)
            memory = load_memory(memory_path, world_id)
            active_world_id = world_id

        memory.advance_step()

        last_action = observation.get("last_action")

        # Failed destinations are excluded from later planning so the brain cannot
        # alternate forever between equivalent approaches to the same obstacle.
        if (last_action and last_action.get("type")
                in {"move_to", "investigate"} and not
                last_action.get("succeeded", False)
                and last_action.get("target") is not None):
            target = tuple(last_action["target"])
            memory.mark_target_failed(target)

            if memory.active_recharge_target == target:
                memory.clear_recharge_target()

            if last_action["type"] == "investigate":
                memory.clear_investigation_target()
            elif memory.active_investigation_approach == target:
                memory.clear_investigation_approach()

            if (
                    memory.active_plan is not None
                    and memory.active_plan.goal == "investigate"
            ):
                memory.clear_active_plan()

            print(f"Failed targets: {memory.failed_targets}", file=sys.stderr)

        if last_action and last_action.get("type") == "investigate" and last_action.get("succeeded", False):
            x, y = last_action["target"]
            target = (x, y)

            revealed_cell = next((cell for cell in observation["visible_cells"] if tuple(cell["position"]) == target),
                                 None, )

            if revealed_cell is not None:
                memory.remember_investigation_result(target, revealed_cell["type"])

            memory.clear_investigation_target()

        for visible_cell in observation["visible_cells"]:
            memory.remember_cell(
                visible_cell["position"],
                visible_cell["type"],
            )

        memory.record_visit(observation["position"])

        for visible_object in observation["nearby_objects"]:
            memory.remember_entity(
                visible_object["position"],
                visible_object["type"],
            )

        # Sensor truth supersedes remembered batteries when a previously known coordinate
        # is inside the current scan but no longer contains a battery.
        visible_batteries = {
            tuple(obj["position"]) for obj in observation["nearby_objects"] if obj["type"] == "Battery"
        }

        aura_x, aura_y = observation["position"]
        sensor_radius = observation["sensor_radius"]

        for battery in memory.batteries():
            battery_x, battery_y = battery

            within_sensor_range = (
                    abs(battery_x - aura_x) <= sensor_radius and abs(battery_y - aura_y) <= sensor_radius
            )

            if (within_sensor_range and battery not in visible_batteries):
                memory.forget_battery(
                    battery
                )

        if memory.active_plan is not None:
            update_plan_from_observation(
                memory.active_plan,
                observation,
            )

            if memory.active_plan.is_complete():
                memory.clear_active_plan()

        save_memory(memory, memory_path, world_id)

        score = goal_scores(observation, memory)
        goal = choose_goal(observation, memory)

        decision = decide(observation, goal, memory)

        # Debug metadata shares the response but is never used to execute the action.
        decision["debug"] = {
            "goal": goal,
            "goal_scores": score,
            "known_cells": [
                list(position) for position in memory.known_cells.keys()
            ],
            "visited_cells": [
                list(position) for position in memory.visit_counts.keys()
            ],
        }

        print(json.dumps(decision), flush=True)


if __name__ == "__main__":
    main()
