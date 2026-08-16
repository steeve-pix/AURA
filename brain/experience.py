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
