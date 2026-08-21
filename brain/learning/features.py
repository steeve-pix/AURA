from dataclasses import dataclass

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

FEATURE_NAMES = (
    "energy",
    "goal_explore",
    "goal_recharge",
    "goal_investigate",
    "action_move",
    "action_move_to",
    "action_investigate",
    "has_target",
    "target_dx",
    "target_dy",
    "path_length",
    "memory_trust",
    "next_step_was_visited",
)

FEATURE_GROUPS = {
    "goal": (
        "goal_explore",
        "goal_recharge",
        "goal_investigate",
    ),
    "action": (
        "action_move",
        "action_move_to",
        "action_investigate",
    ),
    "target_offset": (
        "target_dx",
        "target_dy",
    )
}

ABLATION_NAMES = (
    *FEATURE_NAMES,
    *FEATURE_GROUPS.keys(),
)


@dataclass
class ValueInput:
    energy: int
    goal: str
    action: str
    target: tuple[int, int] | None
    position: tuple[int, int]
    path_length: int | None
    memory_trust: float | None
    next_step_was_visited: bool | None


def encode_energy(value_input: ValueInput) -> float:
    return value_input.energy / 100.0


def encode_goal(value_input: ValueInput) -> list[float]:
    return [1.0 if value_input.goal == goal else 0.0 for goal in GOAL_TYPES]


def encode_action(value_input: ValueInput) -> list[float]:
    return [1.0 if value_input.action == action else 0.0 for action in ACTION_TYPES]


def encode_has_target(value_input: ValueInput) -> float:
    return 1.0 if value_input.target is not None else 0.0


def encode_target_offset(value_input: ValueInput) -> tuple[float, float]:
    if value_input.target is None:
        return 0.0, 0.0

    target_x, target_y = value_input.target
    aura_x, aura_y = value_input.position

    dx, dy = aura_x - target_x, aura_y - target_y

    return (
        dx / POSITION_SCALE,
        dy / POSITION_SCALE
    )


def encode_path_length(value_input: ValueInput) -> float:
    if value_input.path_length is None:
        return 0.0

    return value_input.path_length / PATH_LENGTH_SCALE


def encode_memory_trust(value_input: ValueInput) -> float:
    if value_input.memory_trust is None:
        return 0.0

    return value_input.memory_trust


def encode_next_step_was_visited(value_input: ValueInput) -> float:
    if value_input.next_step_was_visited is None:
        return 0.0
    return 1.0 if value_input.next_step_was_visited else 0.0


def encode_value_input(value_input: ValueInput) -> list[float]:
    energy = encode_energy(value_input)
    goal = encode_goal(value_input)
    action = encode_action(value_input)
    has_target = encode_has_target(value_input)

    dx, dy = encode_target_offset(value_input)

    path_length = encode_path_length(value_input)
    memory_trust = encode_memory_trust(value_input)
    next_step_was_visited = encode_next_step_was_visited(value_input)

    return [
        energy,  # 0.0 <= energy <= 1.0
        *goal,  # [0, 0, 1] | [0, 1, 0] | [1, 0, 0]
        *action,  # [0, 0, 1] | [0, 1, 0] | [1, 0, 0]
        has_target,  # 0 or 1
        dx,  # 0.0 <= dx <= 1.0
        dy,  # 0.0 <= dy <= 1.0
        path_length,  # 0.0 <= path_length <= 1.0
        memory_trust,  # 0.0 <= memory_trust <= 1.0
        next_step_was_visited  # 0.0 <= next_step_was_visited <= 1.0
    ]


def encode_experience(experience: Experience) -> list[float]:
    value_input = ValueInput(
        energy=experience.energy_before,
        goal=experience.goal,
        action=experience.action,
        target=experience.target,
        position=experience.position_before,
        path_length=experience.path_length_before,
        memory_trust=experience.memory_trust_before,
        next_step_was_visited=experience.next_step_was_visited
    )

    return encode_value_input(value_input)
