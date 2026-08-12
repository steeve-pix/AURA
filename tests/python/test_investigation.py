import unittest

from brain.decision import choose_investigation_action
from brain.memory import Memory


class TestInvestigation(unittest.TestCase):
    def test_investigates_visible_unknown_without_moving_onto_it(self):
        observation = {
            "position": [10, 10],
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
            {"action": "investigate", "target": [12, 10]},
        )

    def test_ignores_failed_unknown_target(self):
        memory = Memory()
        memory.mark_target_failed((12, 10))
        observation = {
            "position": [10, 10],
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


if __name__ == "__main__":
    unittest.main()
