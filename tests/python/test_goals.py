import unittest

from brain.goals import choose_goal

class TestGoals(unittest.TestCase):
    def test_low_energy_selects_recharge(self):
        observation = {
            "energy": 20
        }

        self.assertEqual(choose_goal(observation), "recharge")

    def test_high_energy_selects_recharge(self):
        observation = {
            "energy": 80
        }

        self.assertEqual(choose_goal(observation), "explore")


if __name__ == '__main__':
    unittest.main()