"""Choose AURA's next high-level intention from a body observation."""
import random

from brain.goals import investigation_goal_proposals, select_best_investigation_proposal, recharge_goal_proposal, \
    exploration_goal_proposal, investigation_route_cost, route_fits_energy_budget
from brain.memory import Memory
from brain.planning import (
    Plan,
    PlanStep,
    create_recharge_plan as create_recharge_plan_for_target,
)


def create_recharge_search_plan(observation, memory: Memory) -> Plan | None:
    exploration = exploration_goal_proposal(observation, memory)

    if exploration.target is None:
        return None

    return Plan(
        goal="recharge",
        goal_target=None,
        created_step=memory.step,
        last_progress_step=memory.step,
        steps=[
            PlanStep(
                step_type="move_to",
                target=exploration.target,
                requires_reachable_target=True
            )
        ]
    )


def create_recharge_plan(observation, memory: Memory, ) -> Plan | None:
    proposal = recharge_goal_proposal(observation, memory)

    if proposal is None or proposal.target is None:
        return None

    return create_recharge_plan_for_target(proposal.target, created_step=memory.step)


def replan_failed_recharge(observation, memory: Memory, ) -> bool:
    failed_plan = memory.active_plan

    if (
            failed_plan is None
            or failed_plan.goal != "recharge"
            or not failed_plan.has_failed()
    ):
        return False

    memory.record_plan_failure()
    memory.clear_active_plan()
    replacement = create_recharge_plan(observation, memory)

    if replacement is None:
        return False

    memory.set_active_plan(replacement)
    memory.record_replan()
    return True


def choose_recharge_action(observation, memory):
    if memory.active_plan is not None and memory.active_plan.goal == "recharge":
        return action_from_plan(memory.active_plan)

    plan = create_recharge_plan(observation, memory)

    if plan is not None:
        memory.set_active_plan(plan)
        return action_from_plan(plan)

    search_plan = create_recharge_search_plan(observation, memory)

    if search_plan is not None:
        memory.set_active_plan(search_plan)
        return action_from_plan(search_plan)

    return choose_local_exploration_action(observation, memory)


def choose_adjacent_unknown_action(observation, memory: Memory, *,
                                   exclude_target: tuple[int, int] | None) -> dict | None:
    aura_position = (observation["position"][0], observation["position"][1])

    proposals = investigation_goal_proposals(observation, memory)

    adjacent_proposals = [
        proposal for proposal in proposals if proposal.target is not None
                                              and proposal.target != exclude_target and (
                                                      abs(proposal.target[0] - aura_position[0]) + abs(
                                                  proposal.target[1] - aura_position[1])) == 1]

    selected = select_best_investigation_proposal(adjacent_proposals)

    if selected is None or selected.target is None:
        return None

    return {
        "action": "investigate",
        "target": list(selected.target),
    }


def decide(observation, goal, memory):
    if observation["energy"] <= 0:
        return {"action": "idle"}

    if memory.active_plan is not None and memory.active_plan.goal == goal:
        if goal == "investigate":
            opportunistic_action = (
                choose_adjacent_unknown_action(observation, memory, exclude_target=(
                    memory.active_plan.goal_target
                ),
                                               )
            )

            if opportunistic_action is not None:
                return opportunistic_action

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


def create_exploration_plan(observation, memory: Memory) -> Plan | None:
    proposal = exploration_goal_proposal(observation, memory)

    if proposal.target is None:
        return None

    return Plan(
        goal="explore",
        goal_target=proposal.target,
        created_step=memory.step,
        last_progress_step=memory.step,
        steps=[
            PlanStep(
                step_type="move_to",
                target=proposal.target,
                requires_reachable_target=True
            )
        ]
    )


def choose_exploration_action(observation, memory: Memory):
    if memory.active_plan is not None and memory.active_plan.goal == "explore":
        return action_from_plan(memory.active_plan)

    plan = create_exploration_plan(observation, memory)

    if plan is not None:
        memory.set_active_plan(plan)

        return action_from_plan(plan)

    return choose_local_exploration_action(observation, memory)


def choose_local_exploration_action(observation, memory: Memory):
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

    proposals = investigation_goal_proposals(observation, memory)

    selected = select_best_investigation_proposal(proposals)

    if selected is None or selected.target is None:
        return {"action": "idle"}

    target_position = selected.target

    plan = create_investigation_plan(
        observation,
        memory,
        target_position,
    )

    if plan is None:
        return {"action": "idle"}

    memory.set_active_plan(plan)
    return action_from_plan(plan)


def create_investigation_plan(observation, memory: Memory, target_position: tuple[int, int], ) -> Plan | None:
    target_object = next(
        (obj for obj in observation["nearby_objects"] if
         obj["type"] == "Unknown" and tuple(obj["position"]) == target_position), None,
    )

    if target_object is None or not target_object.get("reachable", False) or memory.is_failed_target(target_position):
        return None

    route_cost = investigation_route_cost(target_object, observation)

    if not route_fits_energy_budget(route_cost, observation["energy"]):
        return None

    aura_position = tuple(observation["position"])
    target_x, target_y = target_position

    # Investigation is physical: AURA must share a cardinal edge with the object.
    if abs(target_x - aura_position[0]) + abs(target_y - aura_position[1]) == 1:
        plan = Plan(
            goal="investigate",
            goal_target=target_position,
            created_step=memory.step,
            last_progress_step=memory.step,
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
        created_step=memory.step,
        last_progress_step=memory.step,
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

    memory.record_plan_failure()
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
    memory.record_replan()
    return True
