from dataclasses import dataclass


@dataclass
class Experience:
    step: int
    goal: str
    action: str

    target: tuple[int, int] | None

    position_before: tuple[int, int]
    position_after: tuple[int, int]

    energy_before: int
    energy_after: int

    succeeded: bool

    outcome: str | None = None
    reward: float = 0.0


def detect_outcome(
        pending: dict,
        observation: dict,
) -> str | None:
    if pending["action"] != "investigate":
        return None

    target = pending["target"]

    if target is None:
        return None

    for obj in observation.get("nearby_objects", []):
        if tuple(obj["position"]) == target:
            return obj["type"]

    return None
