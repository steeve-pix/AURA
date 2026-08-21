from brain.experience import (
    Experience,
    RESULT_COMPLETED,
    RESULT_FAILED,
    RESULT_UNREACHABLE,
)


def calculate_reward(experience: Experience) -> float:
    reward = 0.0

    if experience.result == RESULT_UNREACHABLE:
        reward -= 0.5
    elif experience.result == RESULT_FAILED:
        reward -= 0.25
    elif experience.result == RESULT_COMPLETED:
        reward += 0.1

    if experience.visited_new_cell:
        reward += 0.1

    if experience.navigation_progress is not None:
        bounded_progress = max(-3, min(3, experience.navigation_progress))
        reward += bounded_progress * 0.05

    energy_cost = max(0, experience.energy_before - experience.energy_after)

    reward -= energy_cost * 0.01

    if experience.outcome == "Battery":
        reward += 1.0
    elif experience.outcome == "Empty":
        reward += 0.05

    return round(reward, 2)
