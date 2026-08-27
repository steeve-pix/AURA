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

    created_step: int = 0
    last_progress_step: int = 0
    failure_reason: str | None = None

    def mark_failed(self, reason: str) -> None:
        self.failed = True
        self.failure_reason = reason

    def record_progress(self, step: int) -> None:
        self.last_progress_step = step

    def age(self, current_step: int) -> int:
        return max(0, current_step - self.created_step)

    def steps_since_progress(self, current_step: int) -> int:
        return max(0, current_step - self.last_progress_step)

    def is_complete(self) -> bool:
        return self.current_index >= len(self.steps)

    def current_step(self) -> PlanStep | None:
        if self.is_complete():
            return None

        return self.steps[self.current_index]

    def has_failed(self) -> bool:
        return self.failed

    def advance(self, *, current_step: int | None = None) -> None:
        if self.is_complete():
            return

        self.current_index += 1

        if current_step is not None:
            self.record_progress(current_step)


def create_recharge_plan(target: tuple[int, int], *, created_step: int = 0) -> Plan:
    return Plan(
        goal="recharge",
        goal_target=target,
        created_step=created_step,
        last_progress_step=created_step,
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
        "created_step": plan.created_step,
        "last_progress_step": plan.last_progress_step,
        "failure_reason": plan.failure_reason,
    }


def update_plan_from_observation(plan: Plan, observation, *, current_step: int | None = None) -> None:
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
            plan.mark_failed("target_unreachable")
            return

    if step.step_type == "move_to":
        last_action = observation.get("last_action")

        if (last_action and last_action.get("type") == "move_to" and not last_action.get("succeeded", False)
                and last_action.get("target") is not None
                and tuple(last_action["target"]) == step.target):
            body_result = last_action.get("result", "failed")
            plan.mark_failed(f"move_to_{body_result}")
            return

        current_position = tuple(observation["position"])

        if current_position == step.target:
            plan.advance(current_step=current_step)

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
            body_result = last_action.get("result", "failed")
            plan.mark_failed(f"investigate_{body_result}")
            return

        if matching_investigation and last_action.get("succeeded", False):
            plan.advance(current_step=current_step)
