import unittest

from brain.planning import Plan, PlanStep


class PlanTests(unittest.TestCase):
    def test_new_plan_starts_at_first_step(self):
        first_step = PlanStep()
        plan = Plan(goal="Recharge", steps=[first_step, PlanStep()])

        self.assertEqual(plan.current_index, 0)
        self.assertIs(plan.current_step(), first_step)

    def test_current_step_returns_expected_step(self):
        first_step = PlanStep()
        second_step = PlanStep()
        plan = Plan(goal="Recharge", steps=[first_step, second_step])

        plan.advance()

        self.assertIs(plan.current_step(), second_step)

    def test_advance_moves_to_next_step(self):
        first_step = PlanStep()
        second_step = PlanStep()
        plan = Plan(goal="Recharge", steps=[first_step, second_step])

        plan.advance()

        self.assertEqual(plan.current_index, 1)
        self.assertIs(plan.current_step(), second_step)

    def test_advancing_past_final_step_completes_plan(self):
        plan = Plan(goal="Recharge", steps=[PlanStep()])

        plan.advance()

        self.assertTrue(plan.is_complete())
        self.assertIsNone(plan.current_step())

    def test_empty_plan_is_immediately_complete(self):
        plan = Plan(goal="Recharge")

        self.assertTrue(plan.is_complete())
        self.assertIsNone(plan.current_step())


if __name__ == "__main__":
    unittest.main()
