"""Step 131: audit the unchanged reward using classifier-labeled measurements."""
import unittest

from python.diagnostics.unbalanced_stall_audit import run_audit


class UnbalancedStallAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.audit = run_audit()
        cls.results = cls.audit['results']

    def row(self, scenario, candidate='physical_lower_bound'):
        return self.results[scenario][candidate]

    def test_step130_reference_and_common_horizon(self):
        self.assertTrue(self.audit['step130_saved_106_case_verification_validated'])
        self.assertEqual(self.audit['step129_scenarios_replayed_exactly'], 4)
        for candidates in self.results.values():
            for result in candidates.values():
                self.assertLessEqual(result['steps'], 1200)
                self.assertAlmostEqual(result['steps'] / 120,
                                       result['state_seconds']['BALANCED'] + result['state_seconds']['UNBALANCED'] +
                                       result['state_seconds']['FALLEN'])

    def test_classifier_distinguishes_fall_from_unbalanced_truncation(self):
        for name in ('constant_unbalanced_posture', 'constant_near_fall_stall',
                     'oscillate_near_fallen_boundary', 'slow_collapse_below_terminal_angle'):
            row = self.row(name)
            self.assertFalse(row['terminated'], name)
            self.assertTrue(row['truncated'], name)
        fallen = self.row('confirmed_collapse')
        self.assertTrue(fallen['terminated'])
        self.assertEqual(fallen['state_steps']['FALLEN'], 1)

    def test_failure_tail_creates_a_stall_over_fall_preference(self):
        stalled = self.row('constant_near_fall_stall')
        fallen = self.row('confirmed_collapse')
        prompt = self.row('prompt_recovery')
        self.assertGreater(stalled['discounted_adjusted_return'], fallen['discounted_adjusted_return'])
        self.assertGreater(prompt['discounted_adjusted_return'], stalled['discounted_adjusted_return'])
        self.assertEqual(stalled['failure_tail_at_start'], 0.)
        self.assertLess(fallen['failure_tail_at_start'], 0.)

    def test_balanced_oscillation_is_not_a_profitable_boundary_cycle(self):
        standing = self.row('sustained_controlled_standing')
        steady_near = self.row('constant_near_balanced_posture')
        oscillating = self.row('oscillate_near_balanced_boundary')
        self.assertLess(oscillating['discounted_adjusted_return'], standing['discounted_adjusted_return'])
        self.assertLess(oscillating['discounted_adjusted_return'], steady_near['discounted_adjusted_return'])
        self.assertGreater(oscillating['state_seconds']['UNBALANCED'],
                           oscillating['state_seconds']['BALANCED'])

    def test_fall_boundary_oscillation_does_not_beat_constant_stall(self):
        constant = self.row('constant_near_fall_stall')
        oscillating = self.row('oscillate_near_fallen_boundary')
        self.assertLessEqual(oscillating['discounted_adjusted_return'],
                             constant['discounted_adjusted_return'] + .1)

    def test_crouch_is_rewarded_but_not_as_high_as_controlled_standing(self):
        standing = self.row('sustained_controlled_standing')
        crouch = self.row('controlled_crouch')
        self.assertGreater(crouch['discounted_adjusted_return'], 0.)
        self.assertLess(crouch['discounted_adjusted_return'], standing['discounted_adjusted_return'])
        self.assertGreater(crouch['mean_posture_quality'], .9)

    def test_nonfallen_scenarios_are_unchanged_by_failure_utility(self):
        for scenario in ('sustained_controlled_standing', 'prompt_recovery', 'controlled_crouch',
                         'constant_near_balanced_posture', 'constant_near_fall_stall',
                         'slow_collapse_below_terminal_angle'):
            rows = [self.row(scenario, candidate)['discounted_adjusted_return']
                    for candidate in ('stationary_collapse', 'observed_collapse', 'physical_lower_bound')]
            self.assertEqual(rows[0], rows[1], scenario)
            self.assertEqual(rows[1], rows[2], scenario)

    def test_falling_return_sensitivity_tracks_candidate_and_gamma(self):
        rows = [self.row('confirmed_collapse', candidate)['discounted_adjusted_return']
                for candidate in ('stationary_collapse', 'observed_collapse', 'physical_lower_bound')]
        self.assertGreater(rows[0], rows[1])
        self.assertGreater(rows[1], rows[2])
        sensitivity = [row for row in self.audit['sensitivity']
                       if row['scenario'] == 'confirmed_collapse' and row['utility'] == 'physical_lower_bound']
        self.assertEqual({row['gamma'] for row in sensitivity}, {.99, .995, .999, 1.})
        self.assertEqual(len(sensitivity), 4)

    def test_quality_measurements_are_finite_and_reported(self):
        for candidates in self.results.values():
            for row in candidates.values():
                for key in ('mean_posture_quality', 'mean_uncontrolled_motion_penalty',
                            'mean_support_quality', 'discounted_adjusted_return',
                            'undiscounted_adjusted_return', 'terminal_penalty_discounted'):
                    self.assertTrue(__import__('math').isfinite(row[key]), (key, row[key]))


if __name__ == '__main__':
    unittest.main()
