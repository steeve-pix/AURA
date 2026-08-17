import unittest

from brain.experience import (
    Experience,
    RESULT_COMPLETED,
    RESULT_FAILED,
    RESULT_UNREACHABLE,
)
from brain.reward import calculate_reward


def experience(
        *,
        succeeded: bool = True,
        energy_before: int = 90,
        energy_after: int = 90,
        outcome: str | None = None,
        result: str | None = None,
        visited_new_cell: bool = False,
        navigation_progress: int | None = None,
) -> Experience:
    return Experience(
        step=10,
        kind="action",
        event="investigate",
        goal="investigate",
        action="investigate",
        target=(5, 2),
        position_before=(4, 2),
        position_after=(4, 2),
        energy_before=energy_before,
        energy_after=energy_after,
        succeeded=succeeded,
        result=(
            result
            if result is not None
            else RESULT_COMPLETED if succeeded else RESULT_FAILED
        ),
        visited_new_cell=visited_new_cell,
        navigation_progress=navigation_progress,
        outcome=outcome,
    )


class RewardTests(unittest.TestCase):
    def test_success_has_greater_reward_than_identical_failure(self):
        success = calculate_reward(experience(succeeded=True))
        failure = calculate_reward(experience(succeeded=False))

        self.assertGreater(success, failure)

    def test_higher_energy_cost_reduces_reward(self):
        low_cost = calculate_reward(experience(energy_after=89))
        high_cost = calculate_reward(experience(energy_after=85))

        self.assertGreater(low_cost, high_cost)

    def test_battery_outcome_is_more_rewarding_than_no_outcome(self):
        battery = calculate_reward(experience(outcome="Battery"))
        no_outcome = calculate_reward(experience(outcome=None))

        self.assertGreater(battery, no_outcome)

    def test_result_rewards_are_ordered_by_action_quality(self):
        unreachable = calculate_reward(experience(
            succeeded=False,
            result=RESULT_UNREACHABLE,
        ))
        failed = calculate_reward(experience(
            succeeded=False,
            result=RESULT_FAILED,
        ))
        completed = calculate_reward(experience(
            succeeded=True,
            result=RESULT_COMPLETED,
        ))

        self.assertLess(unreachable, failed)
        self.assertLess(failed, completed)

    def test_new_exploration_is_more_rewarding_than_repeated_exploration(self):
        new_cell = calculate_reward(experience(
            visited_new_cell=True,
        ))
        known_cell = calculate_reward(experience(
            visited_new_cell=False,
        ))

        self.assertGreater(new_cell, known_cell)

    def test_navigation_progress_is_more_rewarding_than_regression(self):
        progress = calculate_reward(experience(
            navigation_progress=1,
        ))
        regression = calculate_reward(experience(
            navigation_progress=-1,
        ))

        self.assertGreater(progress, regression)

    def test_navigation_progress_reward_is_bounded(self):
        large_progress = calculate_reward(experience(navigation_progress=20))
        bounded_progress = calculate_reward(experience(navigation_progress=3))
        large_regression = calculate_reward(experience(navigation_progress=-20))
        bounded_regression = calculate_reward(experience(navigation_progress=-3))

        self.assertEqual(large_progress, bounded_progress)
        self.assertEqual(large_regression, bounded_regression)


if __name__ == "__main__":
    unittest.main()
