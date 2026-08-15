from dataclasses import dataclass, field
from typing import Literal

PlanStepType = Literal[
    "move_to",
    "investigate",
    "recharge"
]


class PlanStep:
    step_type: PlanStepType
    target: tuple[int, int] | None = None


@dataclass
class Plan:
    goal: str
    steps: list[PlanStep] = field(default_factory=list)
    current_index: int = 0

    def is_complete(self)->bool:
        return self.current_index >=len(self.steps)

    def current_step(self)->PlanStep|None:
        if self.is_complete():
            return None

        return self.steps[self.current_index]

    def advance(self)->None:
        if not self.is_complete():
            self.current_index += 1