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


def goal_scores(observation, memory):

    return {
        "recharge": recharge_score(observation),
        "explore": explore_score(observation, memory),
    }


def choose_goal(observation, memory):  # pyright: ignore[reportExplicitAny]
    scores = goal_scores(observation, memory)

    return max(scores, key=lambda k: scores[k])
