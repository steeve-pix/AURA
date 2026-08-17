from dataclasses import dataclass, field
from typing import Literal

PlanStepType = Literal[
    "move_to",
    "investigate",
]


@dataclass
class PlanStep:
    step_type: PlanStepType
    target: tuple[int, int] | None = None
    requires_reachable_target: bool = False


@dataclass
class Plan:
    goal: str
    goal_target: tuple[int, int] | None = None
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


def create_recharge_plan(target: tuple[int, int], ) -> Plan:
    return Plan(
        goal="recharge",
        goal_target=target,
        steps=[
            PlanStep(
                step_type="move_to",
                target=target,
                requires_reachable_target=True,
            ),
        ],
    )


def plan_debug(plan: Plan | None) -> dict | None:
    if plan is None:
        return None

    current_step = plan.current_step()

    return {
        "goal": plan.goal,
        "goal_target": None if plan.goal_target is None else list(plan.goal_target),
        "current_step": plan.current_index,
        "step_count": len(plan.steps),
        "failed": plan.has_failed(),
        "step": (
            None
            if current_step is None
            else {
                "type": current_step.step_type,
                "target": (
                    None
                    if current_step.target is None
                    else list(current_step.target)
                ),
            }
        ),
    }


def update_plan_from_observation(plan: Plan, observation, ) -> None:
    step = plan.current_step()

    if step is None:
        return

    if step.requires_reachable_target and step.target is not None:
        observed_target = next(
            (entity for entity in observation.get("nearby_objects", []) if tuple(entity["position"]) == step.target),
            None,
        )

        # Missing evidence is not failure: investigation approach cells are terrain
        # and therefore do not appear in nearby_objects. An explicitly observed,
        # unreachable target is enough to invalidate the step.
        if observed_target is not None and not observed_target.get("reachable", False):
            plan.failed = True
            return

    if step.step_type == "move_to":
        last_action = observation.get("last_action")

        if (last_action and last_action.get("type") == "move_to" and not last_action.get("succeeded", False)
                and last_action.get("target") is not None
                and tuple(last_action["target"]) == step.target):
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

        matching_investigation = (
            last_action.get("type") == "investigate"
            and last_action.get("target") is not None
            and tuple(last_action["target"]) == step.target
        )

        if matching_investigation and not last_action.get("succeeded", False):
            plan.failed = True
            return

        if matching_investigation and last_action.get("succeeded", False):
            plan.advance()
