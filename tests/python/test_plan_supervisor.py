import unittest

from brain.plan_supervisor import (
    review_plan,
)
from brain.planning import Plan, PlanStep
from brain.memory import Memory
from brain.plan_supervisor import review_plan, supervise_goal


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


class PlanSupervisorTests(unittest.TestCase):
    def test_valid_plan_continues(self):
        review = review_plan(
            active_plan(),
            proposed_goal="investigate",
            recharge_urgent=False,
        )

        self.assertEqual(
            review.disposition,
            "continue",
        )

    def test_exploration_does_not_interrupt_plan(self):
        review = review_plan(
            active_plan(),
            proposed_goal="explore",
            recharge_urgent=False,
        )

        self.assertEqual(
            review.disposition,
            "continue",
        )

    def test_urgent_recharge_interrupts_investigation(self):
        review = review_plan(
            active_plan(),
            proposed_goal="recharge",
            recharge_urgent=True,
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
            proposed_goal="investigate",
            recharge_urgent=False,
        )

        self.assertEqual(
            review.disposition,
            "failed",
        )

    def test_complete_plan_is_reported(self):
        plan = Plan(
            goal="investigate",
            steps=[],
        )

        review = review_plan(
            plan,
            proposed_goal="explore",
            recharge_urgent=False,
        )

        self.assertEqual(
            review.disposition,
            "complete",
        )

    def test_recharge_does_not_interrupt_itself(self):
        review = review_plan(
            active_plan(goal="recharge"),
            proposed_goal="recharge",
            recharge_urgent=True,
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
            proposed_goal="explore",
            recharge_urgent=False,
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
            proposed_goal="explore",
            recharge_urgent=False,
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
            proposed_goal="recharge",
            recharge_urgent=True,
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
            proposed_goal="explore",
            recharge_urgent=False,
        )

        self.assertEqual(goal, "explore")
        self.assertIsNone(memory.active_plan)


if __name__ == "__main__":
    unittest.main()
