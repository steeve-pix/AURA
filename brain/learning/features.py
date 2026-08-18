from brain.experience import Experience

GOAL_TYPES = [
    "explore",
    "recharge",
    "investigate",
]

ACTION_TYPES = [
    "move",
    "move_to",
    "investigate"
]

POSITION_SCALE = 50.0
PATH_LENGTH_SCALE = 50.0


def encode_energy(experience: Experience) -> float:
    return experience.energy_before / 100.0


def encode_goal(experience: Experience) -> list[float]:
    return [1.0 if experience.goal == goal else 0.0 for goal in GOAL_TYPES]


def encode_action(experience: Experience) -> list[float]:
    return [1.0 if experience.action == action else 0.0 for action in ACTION_TYPES]


def encode_has_target(experience: Experience) -> float:
    return 1.0 if experience.target is not None else 0.0


def encode_target_offset(experience: Experience) -> tuple[float, float]:
    if experience.target is None:
        return 0.0, 0.0

    target_x, target_y = experience.target
    aura_x, aura_y = experience.position_before

    dx, dy = aura_x - target_x, aura_y - target_y

    return (
        dx / POSITION_SCALE,
        dy / POSITION_SCALE
    )


def encode_path_length(experience: Experience) -> float:
    if experience.path_length_before is None:
        return 0.0

    return experience.path_length_before / PATH_LENGTH_SCALE


def encode_memory_trust(experience: Experience) -> float:
    if experience.memory_trust_before is None:
        return 0.0

    return experience.memory_trust_before


def encode_experience(experience: Experience) -> list[float]:
    energy = encode_energy(experience)
    goal = encode_goal(experience)
    action = encode_action(experience)
    has_target = encode_has_target(experience)

    dx, dy = encode_target_offset(experience)
    path_length = encode_path_length(experience)

    memory_trust = encode_memory_trust(experience)

    return [energy, *goal, *action, has_target, dx, dy, path_length, memory_trust]
