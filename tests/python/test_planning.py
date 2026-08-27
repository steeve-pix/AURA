import unittest

from brain.memory import Memory
from brain.planning import (
    Plan,
    PlanStep,
    create_recharge_plan,
    plan_debug,
    update_plan_from_observation, MAX_STEPS_WITHOUT_PLAN_PROGRESS,
)


class PlanTests(unittest.TestCase):
    def test_move_to_step_can_require_reachability(self):
        step = PlanStep(
            step_type="move_to",
            target=(3, 4),
            requires_reachable_target=True,
        )

        self.assertTrue(step.requires_reachable_target)

    def test_investigate_step_does_not_require_path_reachability(self):
        step = PlanStep(step_type="investigate", target=(4, 4))

        self.assertFalse(step.requires_reachable_target)

    def test_recharge_plan_requires_a_reachable_target(self):
        plan = create_recharge_plan((4, 4))

        self.assertTrue(plan.steps[0].requires_reachable_target)
        self.assertEqual(plan.goal_target, (4, 4))

    def test_failed_reachability_precondition_marks_plan_failed(self):
        plan = Plan(
            goal="recharge",
            steps=[PlanStep(
                step_type="move_to",
                target=(4, 4),
                requires_reachable_target=True,
            )],
        )

        update_plan_from_observation(plan, {
            "position": [1, 1],
            "nearby_objects": [{
                "type": "Battery",
                "position": [4, 4],
                "reachable": False,
                "path_length": -1,
            }],
            "last_action": None,
        })

        self.assertTrue(plan.has_failed())

    def test_valid_reachability_precondition_keeps_step_active(self):
        plan = Plan(
            goal="recharge",
            steps=[PlanStep(
                step_type="move_to",
                target=(4, 4),
                requires_reachable_target=True,
            )],
        )

        update_plan_from_observation(plan, {
            "position": [2, 1],
            "nearby_objects": [{
                "type": "Battery",
                "position": [4, 4],
                "reachable": True,
                "path_length": 5,
            }],
            "last_action": None,
        })

        self.assertFalse(plan.has_failed())
        self.assertEqual(plan.current_index, 0)

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
            goal_target=(12, 5),
            steps=[
                PlanStep(step_type="move_to", target=(11, 5)),
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        )

        self.assertEqual(plan_debug(plan), {
            "goal": "investigate",
            "goal_target": [12, 5],
            "current_step": 0,
            "step_count": 2,
            "failed": False,
            "step": {
                "type": "move_to",
                "target": [11, 5],
            },
            "created_step": 0,
            "last_progress_step": 0,
            "failure_reason": None,
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
        self.assertTrue(plan.has_failed())

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

    def test_plan_records_creation_and_progress_steps(self):
        plan = Plan(
            goal="investigate",
            created_step=10,
            last_progress_step=10,
        )

        self.assertEqual(plan.age(14), 4)
        self.assertEqual(plan.steps_since_progress(14), 4)

        plan.record_progress(13)

        self.assertEqual(plan.steps_since_progress(14), 1)

    def test_plan_records_failure_reason(self):
        plan = Plan(
            goal="investigate",
        )

        plan.mark_failed("unreachable")

        self.assertTrue(plan.has_failed())
        self.assertEqual(plan.failure_reason, "unreachable")

    def test_plan_age_never_becomes_negative(self):
        plan = Plan(
            goal="explore",
            created_step=10,
            last_progress_step=10,
        )

        self.assertEqual(plan.age(8), 0)
        self.assertEqual(plan.steps_since_progress(8), 0)

    def test_arrival_records_plan_progress_step(self):
        plan = Plan(
            goal="recharge",
            goal_target=(4, 2),
            created_step=10,
            last_progress_step=10,
            steps=[
                PlanStep(
                    step_type="move_to",
                    target=(4, 2),
                ),
            ],
        )

        update_plan_from_observation(plan, {"position": [4, 2],
                                            "last_action": {
                                                "type": "move_to",
                                                "target": [4, 2],
                                                "succeeded": True,
                                                "result": "completed",
                                            }, }, current_step=14)

        self.assertTrue(plan.is_complete())
        self.assertEqual(plan.last_progress_step, 14)

    def test_shorter_route_records_progress_without_advancing(self):
        plan = Plan(
            goal="recharge",
            goal_target=(10, 2),
            created_step=10,
            last_progress_step=10,
            steps=[
                PlanStep(
                    step_type="move_to",
                    target=(10, 2),
                ),
            ]
        )

        update_plan_from_observation(plan,
                                     {
                                         "position": [3, 2],
                                         "last_action": {
                                             "type": "move_to",
                                             "target": [10, 2],
                                             "succeeded": True,
                                             "result": "completed",
                                             "path_length_before": 8,
                                             "path_length_after": 7,
                                         },
                                     },
                                     current_step=11,
                                     )

        # The final destination was not reached.
        self.assertEqual(plan.current_index, 0)

        # But navigation genuinely progressed.
        self.assertEqual(plan.last_progress_step, 11)

    def test_equal_route_cost_does_not_record_progress(self):
        plan = Plan(
            goal="recharge",
            goal_target=(10, 2),
            created_step=10,
            last_progress_step=10,
            steps=[
                PlanStep(
                    step_type="move_to",
                    target=(10, 2),
                )
            ]
        )

        update_plan_from_observation(plan, {
            "position": [3, 2],
            "last_action": {
                "type": "move_to",
                "target": [10, 2],
                "succeeded": True,
                "result": "completed",
                "path_length_before": 7,
                "path_length_after": 7,
            }
        }, current_step=11)

        self.assertEqual(plan.last_progress_step, 10)

    def test_plan_fails_after_no_progress_threshold(self):
        plan = Plan(
            goal="recharge",
            goal_target=(10, 2),
            created_step=10,
            last_progress_step=10,
            steps=[
                PlanStep(
                    step_type="move_to",
                    target=(10, 2),
                ),
            ],
        )

        update_plan_from_observation(
            plan,
            {
                "position": [2, 2],
                "last_action": None,
            },
            current_step=(10 + MAX_STEPS_WITHOUT_PLAN_PROGRESS),
        )

        self.assertTrue(plan.has_failed())
        self.assertEqual(plan.failure_reason, "stalled")

    def test_plan_remains_active_before_stall_threshold(self):
        plan = Plan(
            goal="recharge",
            goal_target=(10, 2),
            created_step=10,
            last_progress_step=10,
            steps=[
                PlanStep(
                    step_type="move_to",
                    target=(10, 2),
                ),
            ],
        )

        update_plan_from_observation(
            plan,
            {
                "position": [2, 2],
                "last_action": None,
            },
            current_step=(
                    10
                    + MAX_STEPS_WITHOUT_PLAN_PROGRESS
                    - 1
            ),
        )

        self.assertFalse(plan.has_failed())

    def test_navigation_progress_prevents_stall_at_threshold(self):
        plan = Plan(
            goal="recharge",
            goal_target=(10, 2),
            created_step=10,
            last_progress_step=10,
            steps=[
                PlanStep(
                    step_type="move_to",
                    target=(10, 2),
                ),
            ],
        )

        current_step = 10 + MAX_STEPS_WITHOUT_PLAN_PROGRESS

        update_plan_from_observation(
            plan,
            {
                "position": [3, 2],
                "last_action": {
                    "type": "move_to",
                    "target": [10, 2],
                    "succeeded": True,
                    "result": "completed",
                    "path_length_before": 8,
                    "path_length_after": 7,
                },
            },
            current_step=current_step,
        )

        self.assertFalse(plan.has_failed())
        self.assertEqual(plan.last_progress_step, current_step)


if __name__ == "__main__":
    unittest.main()
