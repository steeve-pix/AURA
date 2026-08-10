import unittest

from brain.decision import decide
from brain.memory import Memory


class DecisionTests(unittest.TestCase):
    def test_recharge_targets_visible_battery(self):
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
                    "position": [6, 2]
                }
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
                "target": [6, 2]
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


if __name__ == "__main__":
    unittest.main()
