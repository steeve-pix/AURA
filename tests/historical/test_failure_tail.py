"""Pure terminal indexing and compatibility tests; no simulations or learning."""
import math
import unittest

from python.failure_tail import discounted_failure_tail


class FailureTailTests(unittest.TestCase):
    def tail(self, step, **kwargs):
        return discounted_failure_tail(step, fallen=True, failure_utility=-.4, **kwargs)

    def test_geometric_series_against_explicit_sum(self):
        for gamma in (0., .5, .99, .995, .999999999999, 1.):
            for step in (1, 10, 1198, 1199, 1200):
                expected = -.4 * math.fsum(gamma ** k for k in range(1, 1200 - step + 1))
                self.assertAlmostEqual(self.tail(step, gamma=gamma), expected, places=10)

    def test_terminal_transition_is_not_included(self):
        gamma = .995
        self.assertAlmostEqual(self.tail(1, horizon=3, gamma=gamma), -.4 * (gamma + gamma ** 2))
        self.assertAlmostEqual(self.tail(1199), -.4 * gamma)
        self.assertEqual(self.tail(1200), 0)

    def test_first_step_and_gamma_one(self):
        self.assertEqual(self.tail(1, gamma=1), -.4 * 1199)
        self.assertEqual(self.tail(1199, gamma=1), -.4)

    def test_empty_and_exceeded_horizon(self):
        for step, horizon in ((0, 0), (10, 10), (11, 10), (1200, 1200)):
            self.assertEqual(self.tail(step, horizon=horizon), 0)

    def test_truncation_and_unbalanced_do_not_absorb(self):
        for step in (1, 100, 1200):
            self.assertEqual(discounted_failure_tail(step, fallen=False, failure_utility=-.65), 0)

    def test_failure_at_horizon_keeps_terminal_reward_only(self):
        terminal_reward = -1.65
        self.assertEqual(terminal_reward + self.tail(1200), terminal_reward)

    def test_zero_utility_and_zero_discount(self):
        self.assertEqual(discounted_failure_tail(1, fallen=True, failure_utility=0), 0)
        self.assertEqual(self.tail(1, gamma=0), 0)

    def test_numerical_finiteness_and_near_one(self):
        self.assertTrue(math.isfinite(self.tail(1, horizon=10 ** 1000)))
        self.assertAlmostEqual(self.tail(1, horizon=100, gamma=1 - 1e-14), -.4 * 99, places=9)
        with self.assertRaises(ValueError):
            discounted_failure_tail(1, fallen=True, failure_utility=-1e308, horizon=100, gamma=1)

    def test_invalid_inputs(self):
        for key, value in (('gamma', float('nan')), ('gamma', float('inf')), ('gamma', -1),
                           ('gamma', 1.01), ('gamma', None), ('horizon', -1), ('horizon', 1.5),
                           ('terminal_step', -1), ('terminal_step', True), ('failure_utility', float('inf'))):
            args = dict(terminal_step=1, fallen=True, failure_utility=-.4)
            args[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                discounted_failure_tail(**args)

    def test_existing_backward_return_recurrence_equivalence(self):
        from python.train_nn import discounted_returns
        rewards = [.8, -.2, -1.4]
        gamma = .995;
        horizon = 8;
        step = len(rewards)
        expected = discounted_returns(rewards + [-.4] * (horizon - step), gamma)[:step]
        corrected = rewards[:-1] + [rewards[-1] + self.tail(step, horizon=horizon, gamma=gamma)]
        actual = discounted_returns(corrected, gamma)
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, places=12)
        self.assertEqual(len(corrected), len(rewards))
        self.assertEqual(rewards, [.8, -.2, -1.4])


if __name__ == '__main__':
    unittest.main()
