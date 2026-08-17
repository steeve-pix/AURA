import unittest

from brain.main import update_active_plan_and_record_events
from brain.memory import Memory
from brain.planning import Plan, PlanStep, create_recharge_plan


class PlanExperienceTests(unittest.TestCase):
    def test_completed_plan_records_plan_completed_event(self):
        memory = Memory()
        memory.set_active_plan(create_recharge_plan((5, 2)))

        events = update_active_plan_and_record_events(memory, {
            "position": [5, 2],
            "energy": 100,
            "nearby_objects": [],
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": True,
                "result": "completed",
            },
        })

        self.assertEqual([event.event for event in events], ["plan_completed"])
        self.assertEqual(events[0].reward, 0.75)
        self.assertIsNone(memory.active_plan)

    def test_failed_investigation_approach_records_failure_and_replan(self):
        memory = Memory()
        failed_approach = (11, 10)
        target = (12, 10)
        memory.set_active_plan(Plan(
            goal="investigate",
            goal_target=target,
            steps=[
                PlanStep(
                    step_type="move_to",
                    target=failed_approach,
                    requires_reachable_target=True,
                ),
                PlanStep(step_type="investigate", target=target),
            ],
        ))
        observation = {
            "position": [10, 10],
            "energy": 80,
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
            "last_action": {
                "type": "move_to",
                "target": [11, 10],
                "succeeded": False,
                "result": "unreachable",
            },
        }

        events = update_active_plan_and_record_events(memory, observation)

        self.assertEqual(
            [event.event for event in events],
            ["plan_failed", "replan"],
        )
        self.assertEqual(events[0].reward, -0.40)
        self.assertEqual(events[1].reward, 0.05)
        self.assertEqual(memory.active_plan.goal_target, target)
        self.assertNotEqual(memory.active_plan.current_step().target, failed_approach)
        self.assertEqual(memory.failure_debug()["plan_failures"], 1)
        self.assertEqual(memory.failure_debug()["replans"], 1)
        self.assertEqual(memory.failure_debug()["failed_targets"], 1)


if __name__ == "__main__":
    unittest.main()
