import unittest

from brain.experience import Experience
from brain.reward import calculate_reward


def experience(
        *,
        succeeded: bool = True,
        energy_before: int = 90,
        energy_after: int = 90,
        outcome: str | None = None,
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


if __name__ == "__main__":
    unittest.main()
