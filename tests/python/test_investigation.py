import unittest

from brain.decision import choose_investigation_action
from brain.memory import Memory


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
