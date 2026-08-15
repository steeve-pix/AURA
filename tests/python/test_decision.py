import unittest

from brain.decision import choose_investigation_action, decide
from brain.goals import choose_goal
from brain.memory import Memory
from brain.planning import (
    Plan,
    PlanStep,
    create_recharge_plan,
    update_plan_from_observation,
)


class DecisionTests(unittest.TestCase):
    def test_goal_change_cancels_incompatible_active_plan(self):
        memory = Memory()
        memory.set_active_plan(Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        ))
        observation = {
            "position": [1, 1],
            "north": "Wall",
            "east": "Wall",
            "south": "Wall",
            "west": "Wall",
        }

        decide(observation, "explore", memory)

        self.assertIsNone(memory.active_plan)

    def test_reaching_battery_completes_recharge_plan(self):
        plan = create_recharge_plan((5, 3))

        update_plan_from_observation(plan, {
            "position": [5, 3],
            "last_action": {
                "type": "move_to",
                "target": [5, 3],
                "succeeded": True,
            },
        })

        self.assertTrue(plan.is_complete())

    def test_failed_navigation_fails_and_clears_recharge_plan(self):
        memory = Memory()
        plan = create_recharge_plan((5, 3))
        memory.set_active_plan(plan)

        update_plan_from_observation(plan, {
            "position": [2, 2],
            "last_action": {
                "type": "move_to",
                "target": [5, 3],
                "succeeded": False,
            },
        })
        if plan.has_failed():
            memory.clear_active_plan()

        self.assertTrue(plan.has_failed())
        self.assertIsNone(memory.active_plan)

    def test_critical_recharge_cancels_investigation_plan(self):
        memory = Memory()
        memory.set_active_goal("investigate")
        memory.set_active_plan(Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(11, 5)),
                PlanStep(step_type="investigate", target=(12, 5)),
            ],
        ))
        observation = {
            "position": [2, 2],
            "energy": 8,
            "north": "Wall",
            "east": "Wall",
            "south": "Wall",
            "west": "Wall",
            "nearby_objects": [],
        }

        goal = choose_goal(observation, memory)
        decide(observation, goal, memory)

        self.assertEqual(goal, "recharge")
        self.assertIsNone(memory.active_plan)

    def test_recharge_plan_locks_selected_battery(self):
        memory = Memory()
        memory.set_active_plan(create_recharge_plan((9, 5)))

        observation = {
            "position": [1, 1],
            "energy": 30,
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [9, 5],
                    "reachable": True,
                    "path_length": 14,
                },
                {
                    "type": "Battery",
                    "position": [7, 4],
                    "reachable": True,
                    "path_length": 12,
                },
            ],
        }

        action = decide(observation, "recharge", memory)

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": [9, 5],
            },
        )

    def test_newly_visible_battery_does_not_replace_recharge_plan(self):
        memory = Memory()
        memory.set_active_plan(create_recharge_plan((9, 5)))

        observation = {
            "position": [1, 1],
            "energy": 30,
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [9, 5],
                    "reachable": True,
                    "path_length": 14,
                },
                {
                    "type": "Battery",
                    "position": [5, 3],
                    "reachable": True,
                    "path_length": 7,
                },
            ],
        }

        action = decide(observation, "recharge", memory)

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": [9, 5],
            },
        )

    def test_recharge_excludes_battery_that_costs_too_much_energy(self):
        memory = Memory()

        observation = {
            "position": [1, 1],
            "energy": 15,
            "north": "Wall",
            "east": "Wall",
            "south": "Wall",
            "west": "Wall",
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [8, 4],
                    "reachable": True,
                    "path_length": 20,
                },
            ],
        }

        action = decide(observation, "recharge", memory)

        self.assertEqual(action, {"action": "idle"})
        self.assertIsNone(memory.active_plan)

    def test_recharge_chooses_shortest_energy_viable_path(self):
        memory = Memory()

        observation = {
            "position": [1, 1],
            "energy": 20,
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [9, 5],
                    "reachable": True,
                    "path_length": 14,
                },
                {
                    "type": "Battery",
                    "position": [5, 3],
                    "reachable": True,
                    "path_length": 7,
                },
            ],
        }

        action = decide(observation, "recharge", memory)

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": [5, 3],
            },
        )

    def test_recharge_chooses_shortest_reachable_path(self):
        memory = Memory()

        observation = {
            "position": [1, 1],
            "energy": 20,
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [3, 1],
                    "reachable": True,
                    "path_length": 20,
                },
                {
                    "type": "Battery",
                    "position": [8, 4],
                    "reachable": True,
                    "path_length": 8,
                },
            ],
        }

        action = decide(observation, "recharge", memory)

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": [8, 4],
            },
        )

    def test_recharge_ignores_geometrically_close_unreachable_battery(self):
        memory = Memory()

        observation = {
            "position": [1, 1],
            "energy": 20,
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [2, 1],
                    "reachable": False,
                    "path_length": -1,
                },
                {
                    "type": "Battery",
                    "position": [8, 4],
                    "reachable": True,
                    "path_length": 8,
                },
            ],
        }

        action = decide(observation, "recharge", memory)

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": [8, 4],
            },
        )

    def test_recharge_selects_shorter_visible_path_without_plan(self):
        memory = Memory()

        observation = {
            "position": [1, 1],
            "energy": 20,
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [2, 1],
                    "reachable": True,
                    "path_length": 1,
                },
            ],
        }

        action = decide(observation, "recharge", memory)

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": [2, 1],
            },
        )
        self.assertEqual(memory.active_plan.steps[0].target, (2, 1))

    def test_recharge_does_not_reselect_failed_active_target(self):
        memory = Memory()
        failed_target = (3, 1)

        memory.mark_target_failed(failed_target)

        observation = {
            "position": [1, 1],
            "energy": 20,
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": list(failed_target),
                    "reachable": True,
                    "path_length": 2,
                },
                {
                    "type": "Battery",
                    "position": [8, 4],
                    "reachable": True,
                    "path_length": 8,
                },
            ],
        }

        action = decide(observation, "recharge", memory)

        self.assertTrue(memory.is_failed_target(failed_target))
        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": [8, 4],
            },
        )
        self.assertEqual(memory.active_plan.steps[0].target, (8, 4))

    def test_recharge_skips_unreachable_visible_batteries(self):
        memory = Memory()

        observation = {
            "position": [2, 2],
            "energy": 20,
            "north": "Empty",
            "east": "Empty",
            "south": "Empty",
            "west": "Empty",
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [3, 2],
                    "reachable": False,
                    "path_length": -1,
                }
            ]
        }

        memory.remember_battery([8, 2])

        action = decide(
            observation,
            "recharge",
            memory
        )

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": [8, 2],
            }
        )

    def test_recharge_uses_remembered_battery(self):
        memory = Memory()
        memory.remember_battery([6, 2])

        observation = {
            "position": [2, 2],
            "energy": 20,
            "north": "Empty",
            "east": "Empty",
            "south": "Empty",
            "west": "Empty",
            "nearby_objects": []
        }

        action = decide(
            observation,
            "recharge",
            memory
        )

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": [6, 2]
            }
        )

    def test_recharge_uses_trust_to_rank_remembered_batteries(self):
        memory = Memory()
        close_old_battery = (3, 2)
        farther_trusted_battery = (6, 2)
        memory.remember_battery(close_old_battery)

        for _ in range(100):
            memory.advance_step()

        for _ in range(5):
            memory.remember_battery(farther_trusted_battery)

        observation = {
            "position": [2, 2],
            "energy": 20,
            "north": "Empty",
            "east": "Empty",
            "south": "Empty",
            "west": "Empty",
            "nearby_objects": [],
        }

        action = decide(observation, "recharge", memory)

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": list(farther_trusted_battery),
            },
        )

    def test_recharge_prefers_visible_battery_over_trusted_memory(self):
        memory = Memory()
        remembered_battery = (3, 2)
        visible_battery = (8, 4)

        for _ in range(5):
            memory.remember_battery(remembered_battery)

        observation = {
            "position": [2, 2],
            "energy": 20,
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": list(visible_battery),
                    "reachable": True,
                    "path_length": 8,
                },
            ],
        }

        action = decide(observation, "recharge", memory)

        self.assertEqual(
            action,
            {
                "action": "move_to",
                "target": list(visible_battery),
            },
        )

    def test_recharge_retries_a_battery_after_one_failure(self):
        memory = Memory()
        memory.remember_battery([6, 2])
        memory.record_failed_target([6, 2])

        observation = {
            "position": [2, 2],
            "energy": 20,
            "north": "Empty",
            "east": "Empty",
            "south": "Empty",
            "west": "Empty",
            "nearby_objects": []
        }

        action = decide(
            observation,
            "recharge",
            memory
        )

        self.assertEqual(action["action"], "move")
        self.assertIn(
            action["direction"],
            {"north", "east", "south", "west"},
        )

    def test_recharge_explores_when_no_viable_battery(self):
        memory = Memory()
        memory.remember_battery([6, 2])
        memory.record_failed_target([6, 2])

        observation = {
            "position": [2, 2],
            "energy": 20,
            "north": "Wall",
            "east": "Wall",
            "south": "Wall",
            "west": "Wall",
            "nearby_objects": []
        }

        action = decide(
            observation,
            "recharge",
            memory
        )

        self.assertEqual(
            action,
            {
                "action": "idle"
            }
        )

    def test_investigation_prefers_historically_rewarding_unknown(self):
        memory = Memory()
        memory.remember_investigation_result([6, 2], "Battery")

        observation = {
            "position": [2, 2],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [5, 2],
                    "reachable": True,
                    "path_length": 3,
                },
                {
                    "type": "Unknown",
                    "position": [6, 2],
                    "reachable": True,
                    "path_length": 4,
                },
            ],
            "visible_cells": [
                {"position": [5, 1], "type": "Empty"},
                {"position": [6, 1], "type": "Empty"},
            ],
        }

        choose_investigation_action(observation, memory)

        self.assertIsNotNone(memory.active_plan)
        self.assertEqual(memory.active_plan.goal, "investigate")
        self.assertEqual(memory.active_plan.steps[-1].target, (6, 2))

    def test_investigation_keeps_existing_target(self):
        memory = Memory()
        memory.set_active_plan(Plan(
            goal="investigate",
            steps=[
                PlanStep(step_type="move_to", target=(5, 1)),
                PlanStep(step_type="investigate", target=(5, 2)),
            ],
        ))
        memory.remember_investigation_result([6, 2], "Battery")

        observation = {
            "position": [2, 2],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [5, 2],
                    "reachable": True,
                    "path_length": 3,
                },
                {
                    "type": "Unknown",
                    "position": [6, 2],
                    "reachable": True,
                    "path_length": 4,
                },
            ],
            "visible_cells": [
                {"position": [5, 1], "type": "Empty"},
                {"position": [6, 1], "type": "Empty"},
            ],
        }

        action = choose_investigation_action(observation, memory)

        self.assertEqual(action, {"action": "move_to", "target": [5, 1]})
        self.assertEqual(memory.active_plan.goal, "investigate")
        self.assertEqual(memory.active_plan.steps[1].target, (5, 2))


if __name__ == "__main__":
    unittest.main()
