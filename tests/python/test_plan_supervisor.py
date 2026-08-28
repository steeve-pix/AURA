import unittest

from brain.goals import GoalProposal
from brain.memory import Memory
from brain.plan_supervisor import review_plan, supervise_goal
from brain.planning import Plan, PlanStep


def active_plan(
        goal: str = "investigate",
) -> Plan:
    return Plan(
        goal=goal,
        goal_target=(5, 5),
        steps=[
            PlanStep(
                step_type="move_to",
                target=(4, 5),
            ),
        ],
    )


def goal_proposal(goal_type, *, target=None, score: float = 0.50, urgency: float = 0.0) -> GoalProposal:
    return GoalProposal(
        goal_type=goal_type,
        target=target,
        score=score,
        urgency=urgency,
        reason="test",
    )


class PlanSupervisorTests(unittest.TestCase):
    def test_valid_plan_continues(self):
        review = review_plan(
            active_plan(),
            proposal=goal_proposal("explore"),
        )

        self.assertEqual(
            review.disposition,
            "continue",
        )

    def test_exploration_does_not_interrupt_plan(self):
        review = review_plan(
            active_plan(),
            proposal=goal_proposal("explore"),
        )

        self.assertEqual(
            review.disposition,
            "continue",
        )

    def test_urgent_recharge_interrupts_investigation(self):
        review = review_plan(
            active_plan(),
            proposal=goal_proposal("recharge", urgency=1.0),
        )

        self.assertEqual(
            review.disposition,
            "interrupt",
        )

    def test_failed_plan_is_reported(self):
        plan = active_plan()
        plan.failed = True

        review = review_plan(
            plan,
            proposal=goal_proposal("investigate"))

        self.assertEqual(
            review.disposition,
            "failed",
        )

    def test_failed_review_preserves_plan_failure_reason(self):
        plan = active_plan()
        plan.mark_failed(
            "stalled"
        )

        review = review_plan(
            plan,
            proposal=goal_proposal(
                "investigate"
            ),
        )

        self.assertEqual(
            review.disposition,
            "failed",
        )
        self.assertEqual(
            review.reason,
            "stalled",
        )

    def test_complete_plan_is_reported(self):
        plan = Plan(
            goal="investigate",
            steps=[],
        )

        review = review_plan(
            plan,
            proposal=goal_proposal("explore"),
        )

        self.assertEqual(
            review.disposition,
            "complete",
        )

    def test_recharge_does_not_interrupt_itself(self):
        review = review_plan(
            active_plan(goal="recharge"),
            proposal=goal_proposal("recharge"),
        )

        self.assertEqual(
            review.disposition,
            "continue",
        )

    def test_advanced_past_final_step_is_complete(self):
        plan = active_plan()
        plan.advance()

        review = review_plan(
            plan,
            proposal=goal_proposal("explore"),
        )

        self.assertEqual(
            review.disposition,
            "complete",
        )

    def test_supervision_keeps_active_plan_over_exploration(self):
        memory = Memory()
        plan = active_plan()

        memory.set_active_plan(plan)
        memory.set_active_goal("investigate")

        goal = supervise_goal(
            memory,
            proposal=goal_proposal("explore"),
        )

        self.assertEqual(goal, "investigate")
        self.assertIs(memory.active_plan, plan)
        self.assertEqual(
            memory.active_goal,
            "investigate",
        )

    def test_supervision_interrupts_plan_for_urgent_recharge(self):
        memory = Memory()

        plan = active_plan()

        memory.set_active_plan(plan)
        memory.set_active_goal("investigate")

        goal = supervise_goal(
            memory,
            proposal=goal_proposal("recharge", urgency=1.0),
        )

        self.assertEqual(goal, "recharge")
        self.assertIsNone(memory.active_plan)
        self.assertEqual(
            memory.active_goal,
            "recharge",
        )

    def test_supervision_uses_proposed_goal_without_plan(self):
        memory = Memory()

        goal = supervise_goal(
            memory,
            proposal=goal_proposal("explore"),
        )

        self.assertEqual(goal, "explore")
        self.assertIsNone(memory.active_plan)
        self.assertEqual(memory.active_goal, "explore")

    def test_supervision_replaces_old_goal_without_plan(self):
        memory = Memory()

        goal = supervise_goal(
            memory,
            proposal=goal_proposal("explore"),
        )

        self.assertEqual(goal, "explore")
        self.assertEqual(memory.active_goal, "explore")

    def test_battery_interrupts_recharge_search_plan(self):
        search_plan = Plan(
            goal="recharge",
            goal_target=None,
            steps=[
                PlanStep(
                    step_type="move_to",
                    target=(4, 3),
                ),
            ],
        )

        proposal = goal_proposal(
            "recharge",
            target=(7, 5),
            score=0.9,
            urgency=0.9,
        )

        review = review_plan(
            search_plan,
            proposal=proposal,
        )

        self.assertEqual(
            review.disposition,
            "interrupt",
        )

        self.assertEqual(
            review.reason,
            "battery_found",
        )

    def test_recharge_search_continues_without_battery(self):
        search_plan = Plan(
            goal="recharge",
            goal_target=None,
            steps=[
                PlanStep(
                    step_type="move_to",
                    target=(4, 3),
                ),
            ],
        )

        proposal = goal_proposal(
            "recharge",
            target=None,
            score=0.9,
            urgency=0.9,
        )

        review = review_plan(
            search_plan,
            proposal=proposal,
        )

        self.assertEqual(
            review.disposition,
            "continue",
        )


if __name__ == "__main__":
    unittest.main()
