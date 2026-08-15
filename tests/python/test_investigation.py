import unittest

from brain.decision import (
    action_from_plan,
    choose_investigation_action,
    decide,
    replan_failed_investigation,
)
from brain.memory import Memory
from brain.planning import Plan, PlanStep, update_plan_from_observation


class TestInvestigation(unittest.TestCase):
    def test_moves_to_adjacent_cell_before_investigating(self):
        observation = {
            "position": [10, 10],
            "visible_cells": [
                {"type": "Empty", "position": [11, 10]},
                {"type": "Wall", "position": [12, 9]},
                {"type": "Wall", "position": [13, 10]},
                {"type": "Wall", "position": [12, 11]},
            ],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [12, 10],
                    "reachable": True,
                    "path_length": 2,
                }
            ],
        }

        action = choose_investigation_action(observation, Memory())

        self.assertEqual(
            action,
            {"action": "move_to", "target": [11, 10]},
        )

    def test_builds_two_step_investigation_plan(self):
        memory = Memory()
        observation = {
            "position": [10, 10],
            "visible_cells": [
                {"type": "Empty", "position": [11, 10]},
                {"type": "Wall", "position": [12, 9]},
                {"type": "Wall", "position": [13, 10]},
                {"type": "Wall", "position": [12, 11]},
            ],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [12, 10],
                    "reachable": True,
                    "path_length": 2,
                }
            ],
        }

        choose_investigation_action(observation, memory)

        plan = memory.active_plan
        self.assertIsNotNone(plan)
        self.assertEqual(plan.goal, "investigate")
        self.assertEqual(plan.goal_target, (12, 10))
        self.assertEqual(plan.current_index, 0)
        self.assertEqual(len(plan.steps), 2)
        self.assertEqual(plan.steps[0].step_type, "move_to")
        self.assertEqual(plan.steps[0].target, (11, 10))
        self.assertTrue(plan.steps[0].requires_reachable_target)
        self.assertEqual(plan.steps[1].step_type, "investigate")
        self.assertEqual(plan.steps[1].target, (12, 10))
        self.assertFalse(plan.steps[1].requires_reachable_target)

    def test_failed_approach_replans_same_goal_through_another_approach(self):
        memory = Memory()
        observation = {
            "position": [10, 10],
            "visible_cells": [
                {"type": "Empty", "position": [11, 10]},
                {"type": "Empty", "position": [12, 9]},
                {"type": "Empty", "position": [13, 10]},
                {"type": "Empty", "position": [12, 11]},
            ],
            "nearby_objects": [{
                "type": "Unknown",
                "position": [12, 10],
                "reachable": True,
                "path_length": 2,
            }],
        }
        choose_investigation_action(observation, memory)
        failed_plan = memory.active_plan
        failed_approach = failed_plan.current_step().target

        failed_observation = {
            **observation,
            "last_action": {
                "type": "move_to",
                "target": list(failed_approach),
                "succeeded": False,
            },
        }
        memory.mark_target_failed(failed_approach)
        update_plan_from_observation(failed_plan, failed_observation)

        replanned = replan_failed_investigation(failed_observation, memory)
        replacement = memory.active_plan

        self.assertTrue(replanned)
        self.assertTrue(failed_plan.has_failed())
        self.assertEqual(failed_plan.goal_target, (12, 10))
        self.assertEqual(replacement.goal_target, (12, 10))
        self.assertNotEqual(replacement.current_step().target, failed_approach)
        self.assertEqual(replacement.current_step().target, (12, 9))

    def test_replanning_fails_cleanly_without_another_approach(self):
        memory = Memory()
        target = (12, 10)
        failed_approach = (11, 10)
        memory.set_active_plan(Plan(
            goal="investigate",
            goal_target=target,
            steps=[PlanStep(
                step_type="move_to",
                target=failed_approach,
                requires_reachable_target=True,
            )],
            failed=True,
        ))
        memory.mark_target_failed(failed_approach)
        observation = {
            "position": [10, 10],
            "visible_cells": [
                {"type": "Empty", "position": [11, 10]},
                {"type": "Wall", "position": [12, 9]},
                {"type": "Wall", "position": [13, 10]},
                {"type": "Wall", "position": [12, 11]},
            ],
            "nearby_objects": [{
                "type": "Unknown",
                "position": [12, 10],
                "reachable": True,
                "path_length": 2,
            }],
        }

        self.assertFalse(replan_failed_investigation(observation, memory))
        self.assertIsNone(memory.active_plan)
        self.assertTrue(memory.is_failed_target(target))

    def test_replanned_approach_can_advance_to_investigate(self):
        memory = Memory()
        target = (12, 10)
        replacement_approach = (12, 9)
        memory.set_active_plan(Plan(
            goal="investigate",
            goal_target=target,
            steps=[PlanStep(
                step_type="move_to",
                target=(11, 10),
                requires_reachable_target=True,
            )],
            failed=True,
        ))
        memory.mark_target_failed((11, 10))
        observation = {
            "position": [10, 10],
            "visible_cells": [
                {"type": "Empty", "position": [11, 10]},
                {"type": "Empty", "position": [12, 9]},
                {"type": "Wall", "position": [13, 10]},
                {"type": "Wall", "position": [12, 11]},
            ],
            "nearby_objects": [{
                "type": "Unknown",
                "position": [12, 10],
                "reachable": True,
                "path_length": 2,
            }],
        }
        self.assertTrue(replan_failed_investigation(observation, memory))

        update_plan_from_observation(memory.active_plan, {
            **observation,
            "position": list(replacement_approach),
            "last_action": {
                "type": "move_to",
                "target": list(replacement_approach),
                "succeeded": True,
            },
        })

        self.assertEqual(memory.active_plan.current_step().step_type, "investigate")
        self.assertEqual(memory.active_plan.current_step().target, target)

    def test_action_from_plan_does_not_advance_plan(self):
        memory = Memory()
        observation = {
            "position": [10, 10],
            "visible_cells": [
                {"type": "Empty", "position": [11, 10]},
            ],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [12, 10],
                    "reachable": True,
                    "path_length": 2,
                }
            ],
        }
        choose_investigation_action(observation, memory)
        plan = memory.active_plan

        action = action_from_plan(plan)

        self.assertEqual(action, {"action": "move_to", "target": [11, 10]})
        self.assertEqual(plan.current_index, 0)

    def test_action_from_plan_handles_step_without_target(self):
        plan = Plan(
            goal="investigate",
            steps=[PlanStep(step_type="investigate")],
        )

        self.assertEqual(action_from_plan(plan), {"action": "idle"})

    def test_investigates_unknown_from_adjacent_cell(self):
        memory = Memory()
        observation = {
            "position": [11, 10],
            "visible_cells": [],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [12, 10],
                    "reachable": True,
                    "path_length": 1,
                }
            ],
        }

        self.assertEqual(
            choose_investigation_action(observation, memory),
            {"action": "investigate", "target": [12, 10]},
        )

    def test_ignores_failed_unknown_target(self):
        memory = Memory()
        memory.mark_target_failed((12, 10))
        observation = {
            "position": [10, 10],
            "visible_cells": [],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [12, 10],
                    "reachable": True,
                    "path_length": 2,
                }
            ],
        }

        self.assertEqual(
            choose_investigation_action(observation, memory),
            {"action": "idle"},
        )

    def test_active_plan_keeps_same_approach_across_observations(self):
        memory = Memory()
        memory.set_active_plan(Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(13, 10)),
                PlanStep(step_type="investigate", target=(14, 10)),
            ],
        ))
        first_observation = {
            "position": [11, 10],
            "visible_cells": [
                {"type": "Empty", "position": [13, 10]},
                {"type": "Empty", "position": [11, 11]},
            ],
            "nearby_objects": [
                {"type": "Unknown", "position": [14, 10], "reachable": True, "path_length": 3},
                {"type": "Unknown", "position": [11, 12], "reachable": True, "path_length": 2},
            ],
        }
        second_observation = {
            **first_observation,
            "position": [12, 10],
            "visible_cells": [
                {"type": "Empty", "position": [13, 10]},
                {"type": "Empty", "position": [14, 9]},
            ],
        }

        first_action = decide(first_observation, "investigate", memory)
        second_action = decide(second_observation, "investigate", memory)

        self.assertEqual(
            first_action,
            {"action": "move_to", "target": [13, 10]},
        )
        self.assertEqual(second_action, first_action)

    def test_removing_approach_field_does_not_restore_oscillation(self):
        memory = Memory()
        memory.set_active_plan(Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(13, 10)),
                PlanStep(step_type="investigate", target=(14, 10)),
            ],
        ))
        observation = {
            "position": [11, 10],
            "visible_cells": [
                {"type": "Empty", "position": [13, 10]},
                {"type": "Empty", "position": [14, 9]},
            ],
            "nearby_objects": [
                {"type": "Unknown", "position": [14, 10], "reachable": True, "path_length": 3},
            ],
        }

        self.assertEqual(
            decide(observation, "investigate", memory),
            {"action": "move_to", "target": [13, 10]},
        )
        self.assertFalse(hasattr(memory, "active_investigation_approach"))


if __name__ == "__main__":
    unittest.main()
