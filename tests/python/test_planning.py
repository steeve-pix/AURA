import unittest

from brain.memory import Memory
from brain.planning import Plan, PlanStep, plan_debug, update_plan_from_observation


class PlanTests(unittest.TestCase):
    def test_new_plan_starts_at_first_step(self):
        first_step = PlanStep(step_type="move_to", target=(3, 4))
        second_step = PlanStep(step_type="investigate", target=(4, 4))
        plan = Plan(goal="investigate", steps=[first_step, second_step])

        self.assertEqual(plan.current_index, 0)
        self.assertIs(plan.current_step(), first_step)

    def test_current_step_returns_expected_step(self):
        first_step = PlanStep(step_type="move_to", target=(3, 4))
        second_step = PlanStep(step_type="investigate", target=(4, 4))
        plan = Plan(goal="investigate", steps=[first_step, second_step])

        plan.advance()

        self.assertIs(plan.current_step(), second_step)

    def test_advance_moves_to_next_step(self):
        first_step = PlanStep(step_type="move_to", target=(3, 4))
        second_step = PlanStep(step_type="investigate", target=(4, 4))
        plan = Plan(goal="investigate", steps=[first_step, second_step])

        plan.advance()

        self.assertEqual(plan.current_index, 1)
        self.assertIs(plan.current_step(), second_step)

    def test_advancing_past_final_step_completes_plan(self):
        plan = Plan(
            goal="investigate",
            steps=[PlanStep(step_type="investigate", target=(4, 4))],
        )

        plan.advance()

        self.assertTrue(plan.is_complete())
        self.assertIsNone(plan.current_step())

    def test_empty_plan_is_immediately_complete(self):
        plan = Plan(goal="Recharge")

        self.assertTrue(plan.is_complete())
        self.assertIsNone(plan.current_step())

    def test_plan_debug_describes_current_step(self):
        plan = Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(11, 5)),
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        )

        self.assertEqual(plan_debug(plan), {
            "goal": "investigate",
            "current_step": 0,
            "step_count": 2,
            "failed": False,
            "step": {
                "type": "move_to",
                "target": [11, 5],
            },
        })

    def test_plan_debug_is_none_without_active_plan(self):
        self.assertIsNone(plan_debug(None))

    def test_successful_intermediate_move_does_not_advance_plan(self):
        plan = Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(11, 5)),
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        )
        observation = {
            "position": [8, 5],
            "last_action": {
                "type": "move_to",
                "target": [11, 5],
                "succeeded": True,
            },
        }

        update_plan_from_observation(plan, observation)

        self.assertEqual(plan.current_index, 0)

    def test_reaching_move_target_advances_to_investigation(self):
        investigate_step = PlanStep(
            step_type="investigate",
            target=(12, 5),
        )
        plan = Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(11, 5)),
                investigate_step,
            ],
        )

        update_plan_from_observation(
            plan,
            {"position": [11, 5], "last_action": None},
        )

        self.assertIs(plan.current_step(), investigate_step)

    def test_successful_matching_investigation_completes_plan(self):
        plan = Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        )

        update_plan_from_observation(plan, {
            "position": [11, 5],
            "last_action": {
                "type": "investigate",
                "target": [12, 5],
                "succeeded": True,
            },
        })

        self.assertTrue(plan.is_complete())

    def test_failed_investigation_does_not_complete_plan(self):
        plan = Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        )

        update_plan_from_observation(plan, {
            "position": [11, 5],
            "last_action": {
                "type": "investigate",
                "target": [12, 5],
                "succeeded": False,
            },
        })

        self.assertFalse(plan.is_complete())

    def test_failed_matching_move_marks_plan_failed(self):
        plan = Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(11, 5)),
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        )

        update_plan_from_observation(plan, {
            "position": [8, 5],
            "last_action": {
                "type": "move_to",
                "target": [11, 5],
                "succeeded": False,
            },
        })

        self.assertTrue(plan.has_failed())
        self.assertEqual(plan.current_index, 0)

    def test_failed_plan_gets_cleared(self):
        memory = Memory()
        plan = Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(11, 5)),
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        )
        memory.set_active_plan(plan)

        update_plan_from_observation(plan, {
            "position": [8, 5],
            "last_action": {
                "type": "move_to",
                "target": [11, 5],
                "succeeded": False,
            },
        })
        if plan.has_failed():
            memory.clear_active_plan()

        self.assertIsNone(memory.active_plan)

    def test_completed_plan_gets_cleared(self):
        memory = Memory()
        plan = Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        )
        memory.set_active_plan(plan)

        update_plan_from_observation(plan, {
            "position": [11, 5],
            "last_action": {
                "type": "investigate",
                "target": [12, 5],
                "succeeded": True,
            },
        })
        if plan.is_complete():
            memory.clear_active_plan()

        self.assertIsNone(memory.active_plan)


if __name__ == "__main__":
    unittest.main()
