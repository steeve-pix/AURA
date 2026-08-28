from dataclasses import dataclass
from typing import Literal

from brain.memory import Memory

GoalType = Literal["explore", "investigate", "recharge"]


@dataclass(frozen=True)
class GoalProposal:
    goal_type: GoalType
    target: tuple[int, int] | None
    score: float
    urgency: float
    reason: str


ENERGY_RESERVE = 5
RECHARGE_CONSIDERATION_THRESHOLD = 70
HISTORICAL_BATTERY_BONUS = 0.15
INVESTIGATION_BASE_SCORE = 0.75
RECHARGE_PLAN_INTERRUPT_SCORE = 0.70
GOAL_SWITCH_MARGIN = 0.1
CRITICAL_ENERGY = 8
INVESTIGATION_DISTANCE_BONUS = 0.10
BATTERY_ARRIVAL_RESERVE = 2
CARDINAL_OFFSET = (
    (0, -1),
    (1, 0),
    (0, 1),
    (-1, 0)
)


def exploration_frontiers(observation, memory: Memory) -> list[tuple[int, int]]:
    current = tuple(observation["position"])

    frontiers = []

    for position, cell_type in memory.known_cells.items():
        if position == current:
            continue

        if cell_type in {"Wall", "Unknown"}:
            continue

        if memory.is_failed_target(position):
            continue

        borders_unmapped_space = any(
            (position[0] + dx, position[1] + dy) not in memory.known_cells for dx, dy in CARDINAL_OFFSET)

        if borders_unmapped_space:
            frontiers.append(position)

    return frontiers


def select_exploration_frontier(observation, memory: Memory) -> tuple[int, int] | None:
    frontiers = exploration_frontiers(observation, memory)

    if not frontiers:
        return None

    current = tuple(observation["position"])

    return min(frontiers, key=lambda position: (memory.visit_count(position), abs(position[0] - current[0]),
                                                abs(position[1] - current[1]), position))


def choose_best_recharge_target(observation, memory: Memory, ) -> tuple[int, int] | None:
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
           and not memory.is_failed_target((obj["position"][0], obj["position"][1]))
    ]

    if visible_batteries:
        best = min(
            visible_batteries,
            key=lambda obj: obj["path_length"],
        )

        return best["position"][0], best["position"][1]

    remembered = [
        battery
        for battery in memory.batteries()
        if battery not in visible_battery_positions
           and not memory.is_failed_target(battery)
           and (
                   abs(battery[0] - observation["position"][0])
                   + abs(battery[1] - observation["position"][1])
           ) <= energy - BATTERY_ARRIVAL_RESERVE
    ]

    if remembered:
        x, y = observation["position"]
        aura_position = (x, y)

        return max(
            remembered,
            key=lambda battery: remembered_battery_score(
                memory,
                battery,
                aura_position,
            ),
        )

    return None


def remembered_battery_score(memory: Memory, battery: tuple[int, int], aura_position: tuple[int, int], ) -> float:
    trust = memory.battery_trust(battery)
    distance = (
            abs(battery[0] - aura_position[0])
            + abs(battery[1] - aura_position[1])
    )

    return trust / (1.0 + distance)


def recharge_goal_proposal(observation, memory: Memory, ) -> GoalProposal | None:
    target = choose_best_recharge_target(observation, memory)

    if target is None:
        return None

    score = recharge_score(observation)

    visible_targets = {tuple(obj["position"]) for obj in observation["nearby_objects"] if
                       obj["type"] == "Battery" and obj.get("reachable", False)}

    reason = "visible_viable_battery" if target in visible_targets else "remembered_viable_battery"

    urgency = score if recharge_is_urgent(observation) else 0.0

    return GoalProposal(
        goal_type="recharge",
        target=target,
        score=score,
        urgency=urgency,
        reason=reason
    )


def investigation_route_cost(obj, observation) -> int:
    path_length = obj.get("path_length")

    if path_length is not None and path_length >= 0:
        return path_length

    aura_x, aura_y = observation["position"]
    target_x, target_y = obj["position"]

    return abs(aura_x - target_x) + abs(aura_y - target_y)


def recharge_is_urgent(observation) -> bool:
    return observation["energy"] <= CRITICAL_ENERGY or recharge_score(observation) >= RECHARGE_PLAN_INTERRUPT_SCORE


def recharge_score(observation):
    energy = observation["energy"]

    shortest_path = shortest_battery_path(observation)

    if shortest_path is not None and energy <= RECHARGE_CONSIDERATION_THRESHOLD:
        # Reserve prevents a technically reachable battery from becoming a zero-energy trap.
        required_energy = shortest_path + ENERGY_RESERVE

        if energy <= required_energy:
            return 1.0

    return 1.0 - energy / 100.0


def explore_score(observation, memory):
    current = tuple(observation["position"])

    visits = memory.visit_count(current)

    base = 0.30
    # Repeated occupancy raises exploration pressure but cannot dominate every other goal.
    repetition_bonus = min(visits * 0.05, 0.30)

    return base + repetition_bonus


def investigation_score(observation, memory):
    reachable_unknowns = [
        obj for obj in observation["nearby_objects"]
        if obj["type"] == "Unknown"
           and obj.get("reachable", False)
           and not memory.is_failed_target(tuple(obj["position"]))
    ]

    if not reachable_unknowns:
        return 0.0

    score = INVESTIGATION_BASE_SCORE

    if any(
            memory.previous_investigation_result(obj["position"]) == "Battery"
            for obj in reachable_unknowns
    ):
        score += HISTORICAL_BATTERY_BONUS

    return min(score, 1.0)


def investigation_goal_proposals(observation, memory) -> list[GoalProposal]:
    proposals = []

    for obj in observation["nearby_objects"]:
        if obj["type"] != "Unknown":
            continue

        if not obj.get("reachable", False):
            continue

        target = (obj["position"][0], obj["position"][1])

        if memory.is_failed_target(target):
            continue

        score = INVESTIGATION_BASE_SCORE
        reason = "reachable_unknown"

        route_cost = investigation_route_cost(obj, observation)

        score += INVESTIGATION_DISTANCE_BONUS / (1.0 + route_cost)

        if memory.previous_investigation_result(target) == "Battery":
            score += HISTORICAL_BATTERY_BONUS
            reason = "historical_promising_unknown"

        proposals.append(GoalProposal(
            goal_type="investigate",
            target=target,
            score=min(score, 1.0),
            urgency=0.0,
            reason=reason,
        ))

    return proposals


def exploration_goal_proposal(observation, memory) -> GoalProposal:
    target = select_exploration_frontier(observation, memory)

    return GoalProposal(
        goal_type="explore",
        target=target,
        score=explore_score(observation, memory),
        urgency=0.0,
        reason="frontier_exploration" if target is not None else "local_exploration",
    )


def goal_proposals(observation, memory) -> list[GoalProposal]:
    proposals = []

    recharge = recharge_goal_proposal(observation, memory)

    if recharge is None:
        recharge_score_value = recharge_score(observation)

        recharge = GoalProposal(
            goal_type="recharge",
            target=None,
            score=recharge_score_value,
            urgency=recharge_score_value if recharge_is_urgent(observation) else 0.0,
            reason="no_viable_battery"
        )

    proposals.append(recharge)
    proposals.append(exploration_goal_proposal(observation, memory))
    proposals.extend(investigation_goal_proposals(observation, memory))

    return proposals


def best_proposal_for_type(proposals: list[GoalProposal], goal_type: GoalType) -> GoalProposal | None:
    matching = [proposal for proposal in proposals if proposal.goal_type == goal_type]

    if not matching:
        return None

    return max(matching, key=lambda proposal: proposal.score)


def select_goal_proposal(proposals: list[GoalProposal], *, current_goal: GoalType | None, energy: int) -> GoalProposal:
    if not proposals:
        raise ValueError("At least one goal proposal is required.")

    urgent = [proposal for proposal in proposals if proposal.urgency > 0.0]

    if urgent:
        return max(urgent, key=lambda proposal: (proposal.urgency, proposal.score))

    if current_goal == "recharge" and energy < 100:
        recharge = best_proposal_for_type(proposals, "recharge")

        if recharge is not None:
            return recharge

    best = max(proposals, key=lambda proposal: proposal.score)

    if current_goal is None:
        return best

    current = best_proposal_for_type(proposals, current_goal)

    if current is None:
        return best

    if best.goal_type == current_goal:
        return best

    if best.score >= current.score + GOAL_SWITCH_MARGIN:
        return best

    return current


def select_best_investigation_proposal(proposals: list[GoalProposal]) -> GoalProposal | None:
    if not proposals:
        return None

    return max(proposals, key=lambda proposal: (proposal.score, proposal.target))


def goal_scores(observation, memory):
    scores: dict[GoalType, float] = {
        "recharge": 0.0,
        "explore": 0.0,
        "investigate": 0.0,
    }

    for proposal in goal_proposals(observation, memory):
        scores[proposal.goal_type] = max(scores[proposal.goal_type], proposal.score)

    return scores


def propose_goal(observation, memory) -> GoalProposal:
    current_goal = memory.active_goal

    if current_goal is not None and goal_completed(current_goal, observation, memory):
        # Treat a completed goal as inactive during
        # reasoning, but do not mutate Memory here.
        current_goal = None

    proposals = goal_proposals(observation, memory)

    return select_goal_proposal(
        proposals,
        current_goal=current_goal,
        energy=observation["energy"]
    )


def shortest_battery_path(observation):
    path_lengths = [
        obj["path_length"] for obj in observation["nearby_objects"] if
        (obj["type"] == "Battery" and obj["reachable"] and obj["path_length"] >= 0)
    ]

    if not path_lengths:
        return None

    return min(path_lengths)


def goal_completed(goal, observation, memory):
    if goal == "recharge":
        return observation["energy"] >= 100

    if goal == "investigate":
        plan = memory.active_plan

        if plan is not None and plan.goal == "investigate":
            return plan.is_complete() or plan.has_failed()

        return not any(
            obj["type"] == "Unknown"
            and not memory.is_failed_target(tuple(obj["position"]))
            for obj in observation["nearby_objects"]
        )

    if goal == "explore":
        return False

    return True
