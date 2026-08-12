import unittest

from brain.decision import decide
from brain.memory import Memory


class DecisionTests(unittest.TestCase):
    def test_recharge_targets_visible_battery_by_path_length(self):
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
                    "reachable": True,
                    "path_length": 40,
                },
                {
                    "type": "Battery",
                    "position": [8, 2],
                    "reachable": True,
                    "path_length": 12,
                },
            ]
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
                "target": [8, 2],
            }
        )

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


if __name__ == "__main__":
    unittest.main()

