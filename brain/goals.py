from typing import Any

from brain import memory


def recharge_score(observation):
    energy = observation["energy"]

    return 1.0 - energy / 100.0


def explore_score(observation, memory):
    current = tuple(observation["position"])

    visits = memory.visit_count(current)

    base = 0.30
    repetition_bonus = min(visits * 0.05, 0.30)

    return base + repetition_bonus


def investigation_score(observation):
    unknown_objects = [
        obj for obj in observation["nearby_objects"] if obj["type"] == "Unknown" and obj["reachable"]]

    if not unknown_objects:
        return 0.0

    return 0.65


def goal_scores(observation, memory):
    return {
        "recharge": recharge_score(observation),
        "explore": explore_score(observation, memory),
        "investigate": investigation_score(observation),
    }


def choose_goal(observation, memory):  # pyright: ignore[reportExplicitAny]
    scores = goal_scores(observation, memory)

    return max(scores, key=lambda k: scores[k])
