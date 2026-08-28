import unittest

from brain.navigation_safety import (
    exploration_route_is_energy_safe, exploration_decision_is_energy_safe,
    navigation_decision_is_energy_safe, recharge_route_is_energy_safe,
)


class NavigationSafetyTests(unittest.TestCase):
    def observation(self, *, energy: int, battery_path: int | None) -> dict:
        nearby_objects = []

        if battery_path is not None:
            nearby_objects.append({
                "type": "Battery",
                "position": [1, 1],
                "reachable": True,
                "path_length": battery_path,
            })

        return {
            "position": [1, 1],
            "energy": energy,
            "nearby_objects":
                nearby_objects,
        }

    def test_exploration_route_fits_energy_budget(self):
        observation = self.observation(
            energy=50,
            battery_path=5,
        )

        preview = {
            "reachable": True,
            "path_length": 20,
            "next_step": [2, 1],
        }

        # 20 outward + 20 back
        # + 5 to battery + 5 reserve = 50
        self.assertTrue(
            exploration_route_is_energy_safe(observation, preview))

    def test_exploration_route_rejects_insufficient_energy(self):
        observation = self.observation(
            energy=49,
            battery_path=5,
        )

        preview = {
            "reachable": True,
            "path_length": 20,
            "next_step": [2, 1],
        }

        self.assertFalse(
            exploration_route_is_energy_safe(observation, preview))

    def test_exploration_route_rejects_unreachable_target(self):
        observation = self.observation(
            energy=100,
            battery_path=5,
        )

        preview = {
            "reachable": False,
            "path_length": None,
            "next_step": None,
        }

        self.assertFalse(
            exploration_route_is_energy_safe(observation, preview))

    def test_exploration_route_needs_known_return_battery(self):
        observation = self.observation(
            energy=100,
            battery_path=None,
        )

        preview = {
            "reachable": True,
            "path_length": 10,
            "next_step": [2, 1],
        }

        self.assertFalse(
            exploration_route_is_energy_safe(observation, preview))

    def test_exploration_decision_uses_matching_preview(self):
        observation = self.observation(
            energy=50,
            battery_path=5,
        )

        action = {
            "action": "move_to",
            "target": [20, 10],
        }

        previews = {
            (20, 10): {
                "reachable": True,
                "path_length": 20,
                "next_step": [2, 1],
            },
        }

        self.assertTrue(
            exploration_decision_is_energy_safe(
                goal="explore",
                action=action,
                observation=observation,
                navigation_previews=previews,
            )
        )

    def test_exploration_route_rejects_missing_path_length(self):
        observation = self.observation(
            energy=100,
            battery_path=5,
        )

        preview = {
            "reachable": True,
            "next_step": None,
        }

        self.assertFalse(
            exploration_route_is_energy_safe(
                observation,
                preview,
            )
        )


    def test_recharge_route_fits_exact_energy_budget(self):
        observation = self.observation(
            energy=12,
            battery_path=10,
        )

        preview = {
            "reachable": True,
            "path_length": 10,
            "next_step": [2, 1],
        }

        self.assertTrue(
            recharge_route_is_energy_safe(
                observation,
                preview,
            )
        )


    def test_recharge_route_rejects_insufficient_energy(self):
        observation = self.observation(
            energy=11,
            battery_path=10,
        )

        preview = {
            "reachable": True,
            "path_length": 10,
            "next_step": [2, 1],
        }

        self.assertFalse(
            recharge_route_is_energy_safe(
                observation,
                preview,
            )
        )

    def test_recharge_route_rejects_unreachable_battery(self):
        observation = self.observation(
            energy=100,
            battery_path=None,
        )

        preview = {
            "reachable": False,
            "path_length": None,
            "next_step": None,
        }

        self.assertFalse(
            recharge_route_is_energy_safe(
                observation,
                preview,
            )
        )

    def test_recharge_search_uses_exploration_safety(self):
        observation = self.observation(
            energy=12,
            battery_path=5,
        )
        action = {
            "action": "move_to",
            "target": [3, 1],
        }
        previews = {
            (3, 1): {
                "reachable": True,
                "path_length": 4,
                "next_step": [2, 1],
            },
        }

        self.assertFalse(
            navigation_decision_is_energy_safe(
                goal="recharge",
                goal_target=None,
                action=action,
                observation=observation,
                navigation_previews=previews,
            )
        )

    def test_recharge_to_known_battery_uses_one_way_safety(self):
        observation = self.observation(
            energy=12,
            battery_path=5,
        )
        action = {
            "action": "move_to",
            "target": [3, 1],
        }
        previews = {
            (3, 1): {
                "reachable": True,
                "path_length": 10,
                "next_step": [2, 1],
            },
        }

        self.assertTrue(
            navigation_decision_is_energy_safe(
                goal="recharge",
                goal_target=(3, 1),
                action=action,
                observation=observation,
                navigation_previews=previews,
            )
        )


if __name__ == "__main__":
    unittest.main()
