from dataclasses import dataclass, field
from typing import Literal

PlanStepType = Literal[
    "move_to",
    "investigate",
    "recharge"
]


@dataclass
class PlanStep:
    step_type: PlanStepType
    target: tuple[int, int] | None = None


@dataclass
class Plan:
    goal: str
    steps: list[PlanStep] = field(default_factory=list)
    current_index: int = 0
    failed: bool = False

    def is_complete(self) -> bool:
        return self.current_index >= len(self.steps)

    def current_step(self) -> PlanStep | None:
        if self.is_complete():
            return None

        return self.steps[self.current_index]

    def has_failed(self) -> bool:
        return self.failed

    def advance(self) -> None:
        if not self.is_complete():
            self.current_index += 1


def update_plan_from_observation(
    plan: Plan,
    observation,
) -> None:
    step = plan.current_step()

    if step is None:
        return

    if step.step_type == "move_to":
        last_action = observation.get("last_action")

        if (
            last_action
            and last_action.get("type") == "move_to"
            and not last_action.get("succeeded", False)
            and last_action.get("target") is not None
            and tuple(last_action["target"]) == step.target
        ):
            plan.failed = True
            return

        current_position = tuple(observation["position"])

        if current_position == step.target:
            plan.advance()

        return

    if step.step_type == "investigate":
        last_action = observation.get("last_action")

        if not last_action:
            return

        if (
            last_action.get("type") == "investigate"
            and last_action.get("succeeded", False)
            and last_action.get("target") is not None
            and tuple(last_action["target"]) == step.target
        ):
            plan.advance()
