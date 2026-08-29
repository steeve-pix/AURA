from dataclasses import dataclass
from typing import Literal

from brain.goals import GoalProposal
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


def review_plan(plan: Plan, *, proposal: GoalProposal) -> PlanReview:
    if plan.has_failed():
        return PlanReview(
            disposition="failed",
            reason=plan.failure_reason or "plan_failed",
        )

    if plan.is_complete():
        return PlanReview(disposition="complete", reason="plan_completed")

    if proposal.goal_type == "recharge" and proposal.urgency and plan.goal != "recharge":
        return PlanReview(disposition="interrupt", reason="urgent_recharge", )

    if plan.goal == "recharge" and plan.goal_target is None and proposal.goal_type == "recharge" and proposal.target is not None:
        return PlanReview(disposition="interrupt", reason="battery_found")

    return PlanReview(disposition="continue", reason="plan_still_valid")


def supervise_goal(memory: Memory, *, proposal: GoalProposal) -> str:
    plan = memory.active_plan
    proposed_goal = proposal.goal_type

    if plan is None:
        memory.set_active_goal(proposed_goal)

        return proposed_goal

    review = review_plan(plan, proposal=proposal)

    if review.disposition == "continue":
        memory.set_active_goal(plan.goal)

        return plan.goal

    if review.disposition == "interrupt" and review.reason == "urgent_recharge" and plan.goal == "explore" and plan.goal_target is not None:
        memory.mark_target_failed(plan.goal_target)

    if review.disposition in {"complete", "failed", "interrupt"}:
        memory.clear_active_plan()
        memory.set_active_goal(proposed_goal)
        return proposed_goal

    raise ValueError(
        f"Unknown plan disposition: "
        f"{review.disposition}"
    )
