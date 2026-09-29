import math

FALL_ANGLE_RADIANS = math.radians(45.0)


def has_fallen(observation):
    no_foot_support = not (
            observation["left_foot_contact"] or observation["right_foot_contact"]
    )
    torso_tipped_over = abs(observation["torso_angle"]) > FALL_ANGLE_RADIANS
    return no_foot_support or torso_tipped_over


def standing_reward(observation):
    if has_fallen(observation):
        return 0.0

    angle_fraction = abs(observation["torso_angle"]) / FALL_ANGLE_RADIANS
    return 1.0 - angle_fraction
