import unittest

from brain.decision import action_from_plan, choose_investigation_action
from brain.memory import Memory
from brain.planning import Plan, PlanStep


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
        self.assertEqual(plan.current_index, 0)
        self.assertEqual(len(plan.steps), 2)
        self.assertEqual(plan.steps[0].step_type, "move_to")
        self.assertEqual(plan.steps[0].target, (11, 10))
        self.assertEqual(plan.steps[1].step_type, "investigate")
        self.assertEqual(plan.steps[1].target, (12, 10))

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

    def test_reaching_approach_advances_to_investigation(self):
        memory = Memory()
        memory.set_active_plan(Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(11, 10)),
                PlanStep(step_type="investigate", target=(12, 10)),
            ],
        ))
        observation = {
            "position": [11, 10],
            "visible_cells": [],
            "nearby_objects": [{
                "type": "Unknown",
                "position": [12, 10],
                "reachable": True,
                "path_length": 1,
            }],
        }

        action = choose_investigation_action(observation, memory)

        self.assertEqual(
            action,
            {"action": "investigate", "target": [12, 10]},
        )
        self.assertEqual(memory.active_plan.current_index, 1)

    def test_action_from_plan_handles_step_without_target(self):
        plan = Plan(
            goal="investigate",
            steps=[PlanStep(step_type="investigate")],
        )

        self.assertEqual(action_from_plan(plan), {"action": "idle"})

    def test_successful_investigation_cleanup_clears_active_plan(self):
        memory = Memory()
        memory.set_investigation_target((12, 10))
        memory.set_active_plan(Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="investigate", target=(12, 10)),
            ],
        ))

        memory.clear_investigation_target()

        self.assertIsNone(memory.active_plan)

    def test_failed_approach_cleanup_clears_active_plan(self):
        memory = Memory()
        memory.set_investigation_target((12, 10))
        memory.set_investigation_approach((11, 10))
        memory.set_active_plan(Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(11, 10)),
                PlanStep(step_type="investigate", target=(12, 10)),
            ],
        ))

        memory.clear_investigation_approach()

        self.assertIsNone(memory.active_plan)

    def test_investigates_unknown_from_adjacent_cell(self):
        memory = Memory()
        memory.set_investigation_target((12, 10))
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

    def test_keeps_locked_target_and_approach(self):
        memory = Memory()
        memory.set_investigation_target((14, 10))
        memory.set_investigation_approach((13, 10))
        observation = {
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

        self.assertEqual(
            choose_investigation_action(observation, memory),
            {"action": "move_to", "target": [13, 10]},
        )


if __name__ == "__main__":
    unittest.main()
