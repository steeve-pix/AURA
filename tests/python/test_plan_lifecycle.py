import unittest

from brain.decision import decide
from brain.goals import recharge_is_urgent, propose_goal
from brain.memory import Memory
from brain.plan_supervisor import supervise_goal
from brain.planning import update_plan_from_observation


def clear_finished_plan(memory: Memory) -> None:
    plan = memory.active_plan

    if plan is not None and (plan.is_complete() or plan.has_failed()):
        memory.clear_active_plan()


class PlanLifecycleTests(unittest.TestCase):
    def test_investigation_lifecycle(self):
        memory = Memory()
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

        move_action = decide(observation, "investigate", memory)
        plan = memory.active_plan

        self.assertEqual(move_action, {"action": "move_to", "target": [11, 10]})
        self.assertIsNotNone(plan)

        update_plan_from_observation(plan, {
            **observation,
            "position": [11, 10],
            "last_action": {
                "type": "move_to",
                "target": [11, 10],
                "succeeded": True,
            },
        })

        investigate_action = decide(observation, "investigate", memory)
        self.assertEqual(
            investigate_action,
            {"action": "investigate", "target": [12, 10]},
        )

        update_plan_from_observation(plan, {
            **observation,
            "position": [11, 10],
            "last_action": {
                "type": "investigate",
                "target": [12, 10],
                "succeeded": True,
            },
        })
        clear_finished_plan(memory)

        self.assertTrue(plan.is_complete())
        self.assertIsNone(memory.active_plan)

    def test_recharge_lifecycle(self):
        memory = Memory()
        observation = {
            "position": [1, 1],
            "energy": 20,
            "nearby_objects": [{
                "type": "Battery",
                "position": [5, 3],
                "reachable": True,
                "path_length": 6,
            }],
        }

        first_action = decide(observation, "recharge", memory)
        plan = memory.active_plan

        self.assertEqual(first_action, {"action": "move_to", "target": [5, 3]})
        self.assertIsNotNone(plan)

        update_plan_from_observation(plan, {
            **observation,
            "position": [2, 1],
            "last_action": {
                "type": "move_to",
                "target": [5, 3],
                "succeeded": True,
            },
        })
        self.assertFalse(plan.is_complete())
        self.assertEqual(decide(observation, "recharge", memory), first_action)

        update_plan_from_observation(plan, {
            **observation,
            "position": [5, 3],
            "last_action": {
                "type": "move_to",
                "target": [5, 3],
                "succeeded": True,
            },
        })
        clear_finished_plan(memory)

        self.assertTrue(plan.is_complete())
        self.assertIsNone(memory.active_plan)

    def test_energy_emergency_replaces_investigation_with_recharge_plan(self):
        memory = Memory()
        investigation_observation = {
            "position": [10, 10],
            "visible_cells": [{"type": "Empty", "position": [11, 10]}],
            "nearby_objects": [{
                "type": "Unknown",
                "position": [12, 10],
                "reachable": True,
                "path_length": 2,
            }],
        }
        decide(investigation_observation, "investigate", memory)
        investigation_plan = memory.active_plan

        emergency_observation = {
            "position": [10, 10],
            "energy": 8,
            "nearby_objects": [{
                "type": "Battery",
                "position": [12, 9],
                "reachable": True,
                "path_length": 2,
            }],
        }

        proposal = propose_goal(emergency_observation, memory)
        proposed_goal = proposal.goal_type

        goal = supervise_goal(memory, proposed_goal=proposed_goal,
                              recharge_urgent=recharge_is_urgent(emergency_observation))
        action = decide(emergency_observation, goal, memory)

        self.assertEqual(goal, "recharge")
        self.assertEqual(action, {"action": "move_to", "target": [12, 9]})
        self.assertIsNot(memory.active_plan, investigation_plan)
        self.assertEqual(memory.active_plan.goal, "recharge")

    def test_failed_move_clears_plan_and_allows_replanning(self):
        memory = Memory()
        first_observation = {
            "position": [1, 1],
            "energy": 20,
            "nearby_objects": [{
                "type": "Battery",
                "position": [5, 3],
                "reachable": True,
                "path_length": 6,
            }],
        }
        decide(first_observation, "recharge", memory)
        failed_plan = memory.active_plan

        update_plan_from_observation(failed_plan, {
            **first_observation,
            "last_action": {
                "type": "move_to",
                "target": [5, 3],
                "succeeded": False,
            },
        })
        memory.mark_target_failed((5, 3))
        clear_finished_plan(memory)

        replanning_observation = {
            "position": [1, 1],
            "energy": 20,
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [5, 3],
                    "reachable": True,
                    "path_length": 6,
                },
                {
                    "type": "Battery",
                    "position": [4, 2],
                    "reachable": True,
                    "path_length": 4,
                },
            ],
        }
        action = decide(replanning_observation, "recharge", memory)

        self.assertTrue(failed_plan.has_failed())
        self.assertEqual(action, {"action": "move_to", "target": [4, 2]})
        self.assertEqual(memory.active_plan.steps[0].target, (4, 2))


if __name__ == "__main__":
    unittest.main()
