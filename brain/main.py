import json
import sys
from typing import Any
from pathlib import Path

from brain.decision import (decide, replan_failed_investigation, replan_failed_recharge)
from brain.experience import Experience
from brain.goals import choose_goal, goal_scores
from brain.experience_store import append_experience, experience_path_for_world
from brain.learning.features import ValueInput, encode_value_input
from brain.learning.inference import predict_value
from brain.memory_store import load_memory, memory_path_for_world, save_memory
from brain.planning import plan_debug, update_plan_from_observation
from brain.learning.model_io import load_model

PLAN_FAILED_REWARD = -0.40
REPLAN_REWARD = 0.05
PLAN_COMPLETED_REWARD = 0.75


def persist_experience(experience, memory_directory: Path, world_id: str) -> None:
    append_experience(
        experience,
        experience_path_for_world(memory_directory, world_id),
    )


def update_active_plan_and_record_events(
        memory,
        observation: dict,
) -> list[Experience]:
    active_plan = memory.active_plan

    if active_plan is None:
        return []

    events = []
    update_plan_from_observation(active_plan, observation)

    if active_plan.is_complete():
        events.append(memory.record_plan_event(
            event="plan_completed",
            goal=active_plan.goal,
            target=active_plan.goal_target,
            observation=observation,
            reward=PLAN_COMPLETED_REWARD,
        ))
        memory.clear_active_plan()
        return events

    if not active_plan.has_failed():
        return events

    failed_step = active_plan.current_step()

    if failed_step is not None and failed_step.target is not None:
        memory.mark_target_failed(failed_step.target)

    events.append(memory.record_plan_event(
        event="plan_failed",
        goal=active_plan.goal,
        target=active_plan.goal_target,
        observation=observation,
        reward=PLAN_FAILED_REWARD,
    ))

    replanned = (
            replan_failed_investigation(observation, memory)
            or replan_failed_recharge(observation, memory)
    )

    if not replanned:
        memory.clear_active_plan()
        return events

    replacement_plan = memory.active_plan
    events.append(memory.record_plan_event(
        event="replan",
        goal=replacement_plan.goal,
        target=replacement_plan.goal_target,
        observation=observation,
        reward=REPLAN_REWARD,
    ))
    return events


def main() -> None:
    memory_directory = Path("data")

    memory = None
    active_world_id = None
    memory_path = None

    model_path = Path("data/models/value_model.pt")
    value_model = None

    if model_path.exists():
        value_model = load_model(model_path)

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

        completed_experience = memory.finish_pending_experience(observation)

        if completed_experience is not None:
            persist_experience(
                completed_experience,
                memory_directory,
                world_id,
            )

        last_action = observation.get("last_action")

        # Failed destinations are excluded from later planning so the brain cannot
        # alternate forever between equivalent approaches to the same obstacle.
        if (last_action and last_action.get("type")
                in {"move_to", "investigate"} and not
                last_action.get("succeeded", False)
                and last_action.get("target") is not None):
            target = tuple(last_action["target"])
            memory.mark_target_failed(target)

        if last_action and last_action.get("type") == "investigate" and last_action.get("succeeded", False):
            x, y = last_action["target"]
            target = (x, y)

            revealed_cell = next((cell for cell in observation["visible_cells"] if tuple(cell["position"]) == target),
                                 None, )

            if revealed_cell is not None:
                memory.remember_investigation_result(target, revealed_cell["type"])

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

            if within_sensor_range and battery not in visible_batteries:
                memory.forget_battery(
                    battery
                )

        plan_events = update_active_plan_and_record_events(
            memory,
            observation,
        )

        for plan_event in plan_events:
            persist_experience(
                plan_event,
                memory_directory,
                world_id,
            )

        save_memory(memory, memory_path, world_id)

        score = goal_scores(observation, memory)
        goal = choose_goal(observation, memory)

        decision = decide(observation, goal, memory)

        pending = memory.begin_experience(goal=goal, action=decision, observation=observation)

        if value_model is not None and pending is not None:
            value_input = ValueInput(
                energy=pending["energy_before"],
                goal=pending["goal"],
                action=pending["action"],
                target=pending["target"],
                position=pending["position_before"],
                path_length=None,
                memory_trust=pending["memory_trust_before"],
            )

            feature_vector = encode_value_input(value_input)

            predicted_reward = predict_value(value_model, feature_vector)

            print(
                f"[value-model] "
                f"goal: {goal} "
                f"action: {decision['action']} "
                f"predicted_reward: {predicted_reward:+.3f}",
                file=sys.stderr,
            )

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
            "plan": plan_debug(memory.active_plan),
            "failures": memory.failure_debug(),
        }

        print(json.dumps(decision), flush=True)


if __name__ == "__main__":
    main()
