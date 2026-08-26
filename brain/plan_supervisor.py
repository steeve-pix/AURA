from dataclasses import dataclass
from typing import Literal

from brain.memory import Memory
from brain.planning import Plan

PlanDisposition = Literal[
    "continue",
    "complete",
    "failed",
    "interrupt",
]


@dataclass(frozen=True)
class PlanReview:
    disposition: PlanDisposition
    reason: str


def review_plan(plan: Plan, *, proposed_goal: str, recharge_urgent: bool) -> PlanReview:
    if plan.has_failed():
        return PlanReview(disposition="failed", reason="plan_failed")

    if plan.is_complete():
        return PlanReview(disposition="complete", reason="plan_completed")

    if proposed_goal == "recharge" and recharge_urgent and plan.goal != "recharge":
        return PlanReview(disposition="interrupt", reason="urgent_recharge", )

    return PlanReview(disposition="continue", reason="plan_still_valid")


def supervise_goal(memory: Memory, *, proposed_goal: str, recharge_urgent: bool) -> str:
    plan = memory.active_plan

    if plan is None:
        return proposed_goal

    review = review_plan(
        plan, proposed_goal=proposed_goal, recharge_urgent=recharge_urgent
    )

    if review.disposition == "continue":
        memory.set_active_goal(plan.goal)

        return plan.goal

    if review.disposition in {"complete", "failed", "interrupt"}:
        memory.clear_active_plan()
        memory.set_active_goal(proposed_goal)
        return proposed_goal

    raise ValueError(
        f"Unknown plan disposition: "
        f"{review.disposition}"
    )
