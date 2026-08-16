from brain.experience import Experience


def calculate_reward(experience: Experience) -> float:
    reward = 0.0

    if experience.succeeded:
        reward += 0.1
    else:
        reward -= 0.2

    energy_cost = max(
        0,
        experience.energy_before - experience.energy_after,
    )

    reward -= energy_cost * 0.01

    if experience.outcome == "Battery":
        reward += 1.0

    return reward
