import unittest

from brain.memory import Memory
from brain.reward import calculate_reward


class ExperienceTests(unittest.TestCase):
    def test_begin_creates_pending_experience(self):
        memory = Memory()
        memory.step = 10

        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": [5, 2]},
            observation={"position": [2, 2], "energy": 90},
        )

        self.assertIsNotNone(memory.pending_experience)
        self.assertEqual(memory.pending_experience["step"], 10)
        self.assertEqual(memory.pending_experience["goal"], "recharge")
        self.assertEqual(memory.pending_experience["target"], (5, 2))

    def test_experience_finishes_from_next_observation(self):
        memory = Memory()
        memory.step = 10
        before = {
            "position": [2, 2],
            "energy": 90,
        }
        action = {
            "action": "move_to",
            "target": [5, 2],
        }

        memory.begin_experience(
            goal="recharge",
            action=action,
            observation=before,
        )

        after = {
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": True,
            },
        }
        memory.finish_pending_experience(after)

        self.assertEqual(len(memory.experiences), 1)
        experience = memory.experiences[0]
        self.assertEqual(experience.position_before, (2, 2))
        self.assertEqual(experience.position_after, (3, 2))
        self.assertEqual(experience.energy_before, 90)
        self.assertEqual(experience.energy_after, 89)

    def test_body_success_is_copied_to_experience(self):
        memory = Memory()
        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": [5, 2]},
            observation={"position": [2, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [2, 2],
            "energy": 90,
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": False,
            },
        })

        self.assertFalse(memory.experiences[0].succeeded)

    def test_idle_action_does_not_create_pending_experience(self):
        memory = Memory()

        memory.begin_experience(
            goal="explore",
            action={"action": "idle"},
            observation={"position": [2, 2], "energy": 90},
        )

        self.assertIsNone(memory.pending_experience)
        self.assertEqual(memory.experiences, [])

    def test_completed_experience_clears_pending_state(self):
        memory = Memory()
        memory.begin_experience(
            goal="explore",
            action={"action": "move", "direction": "east"},
            observation={"position": [2, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move",
                "target": None,
                "succeeded": True,
            },
        })

        self.assertEqual(len(memory.experiences), 1)
        self.assertIsNone(memory.pending_experience)

    def test_investigation_outcome_is_detected_from_next_observation(self):
        memory = Memory()
        memory.begin_experience(
            goal="investigate",
            action={"action": "investigate", "target": [5, 2]},
            observation={"position": [4, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [4, 2],
            "energy": 90,
            "nearby_objects": [{
                "type": "Battery",
                "position": [5, 2],
                "reachable": True,
                "path_length": 1,
            }],
            "last_action": {
                "type": "investigate",
                "target": [5, 2],
                "succeeded": True,
            },
        })

        self.assertEqual(memory.experiences[0].outcome, "Battery")

    def test_recorded_experience_contains_calculated_reward(self):
        memory = Memory()
        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": [5, 2]},
            observation={"position": [2, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "nearby_objects": [],
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": True,
            },
        })

        recorded = memory.experiences[0]
        self.assertEqual(recorded.reward, calculate_reward(recorded))


if __name__ == "__main__":
    unittest.main()
