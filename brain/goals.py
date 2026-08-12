from typing import Any


def goal_score(observation):
    energy = observation["energy"]

    return {
        "recharge": 1.0 - (energy / 100.0),
        "explore": .40
    }


def choose_goal(observation):  # pyright: ignore[reportExplicitAny]
    scores = goal_score(observation)

    return max(scores, key=lambda k: scores[k])
