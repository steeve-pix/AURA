import unittest
from unittest.mock import patch

from brain.goals import choose_goal, recharge_score
from brain.memory import Memory


class TestGoals(unittest.TestCase):
    def setUp(self):
        self.observation = {
            "energy": 80,
            "position": [4, 2],
            "nearby_objects": [],
        }

    @patch("brain.goals.goal_scores")
    def test_no_active_goal_chooses_highest_score(self, mocked_scores):
        mocked_scores.return_value = {
            "recharge": 0.20,
            "explore": 0.60,
            "investigate": 0.75,
        }
        memory = Memory()

        self.assertEqual(choose_goal(self.observation, memory), "investigate")
        self.assertEqual(memory.active_goal, "investigate")

    @patch("brain.goals.goal_scores")
    def test_current_goal_stays_when_challenger_is_within_margin(self, mocked_scores):
        mocked_scores.return_value = {
            "recharge": 0.20,
            "explore": 0.60,
            "investigate": 0.65,
        }
        memory = Memory()
        memory.set_active_goal("explore")

        self.assertEqual(choose_goal(self.observation, memory), "explore")
        self.assertEqual(memory.active_goal, "explore")

    @patch("brain.goals.goal_scores")
    def test_switches_when_challenger_clearly_wins(self, mocked_scores):
        mocked_scores.return_value = {
            "recharge": 0.20,
            "explore": 0.60,
            "investigate": 0.75,
        }
        memory = Memory()
        memory.set_active_goal("explore")

        self.assertEqual(choose_goal(self.observation, memory), "investigate")
        self.assertEqual(memory.active_goal, "investigate")

    @patch("brain.goals.goal_scores")
    def test_keeps_current_goal_when_it_remains_best(self, mocked_scores):
        mocked_scores.return_value = {
            "recharge": 0.20,
            "explore": 0.70,
            "investigate": 0.65,
        }
        memory = Memory()
        memory.set_active_goal("explore")

        self.assertEqual(choose_goal(self.observation, memory), "explore")
        self.assertEqual(memory.active_goal, "explore")

    @patch("brain.goals.goal_scores")
    def test_critical_energy_forces_recharge(self, mocked_scores):
        mocked_scores.return_value = {
            "recharge": 0.85,
            "explore": 0.60,
            "investigate": 0.95,
        }
        observation = dict(self.observation, energy=8)
        memory = Memory()
        memory.set_active_goal("investigate")

        self.assertEqual(choose_goal(observation, memory), "recharge")
        self.assertEqual(memory.active_goal, "recharge")
        mocked_scores.assert_not_called()

    def test_route_cost_makes_recharge_urgent(self):
        observation = dict(
            self.observation,
            energy=30,
            nearby_objects=[
                {
                    "type": "Battery",
                    "position": [12, 8],
                    "reachable": True,
                    "path_length": 26,
                },
            ],
        )

        self.assertEqual(recharge_score(observation), 1.0)

    def test_short_route_does_not_create_emergency_recharge(self):
        observation = dict(
            self.observation,
            energy=80,
            nearby_objects=[
                {
                    "type": "Battery",
                    "position": [6, 2],
                    "reachable": True,
                    "path_length": 10,
                },
            ],
        )

        self.assertAlmostEqual(recharge_score(observation), 0.2)


if __name__ == '__main__':
    unittest.main()
