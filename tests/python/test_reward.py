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
) -> Experience:
    return Experience(
        step=10,
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


if __name__ == "__main__":
    unittest.main()
