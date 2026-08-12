import unittest

from brain.goals import choose_goal
from brain.memory import Memory


class TestGoals(unittest.TestCase):
    def test_low_energy_selects_recharge(self):
        observation = {
            "energy": 20,
            "position": [4,2]
        }

        memory = Memory()

        self.assertEqual(choose_goal(observation,memory), "recharge")

    def test_high_energy_selects_recharge(self):
        observation = {
            "energy": 80,
            "position": [4,2]
        }

        memory = Memory()

        self.assertEqual(choose_goal(observation,memory), "explore")


if __name__ == '__main__':
    unittest.main()