"""Replay all Step 129 cases without a simulator tick or optimizer update."""
import csv
import json
import math
import unittest

from python.diagnostics.absorbing_failure_audit import ROOT, run_audit
from python.train_nn import discounted_returns


class AbsorbingAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.audit = run_audit()
        cls.previous = json.loads((ROOT / 'docs/traces/step129-incentives/summary.json').read_text())

    def test_all_106_original_objectives_reproduce_exactly(self):
        self.assertEqual(self.audit['original_case_count'], 106)
        self.assertTrue(self.audit['original_scores_exact'])
        for data in self.audit['primary'].values():
            for name, row in data['results'].items():
                self.assertEqual(row['original_return'], self.previous['results'][name]['discounted_return'])

    def test_zero_utility_preserves_every_return_and_all_12_failures(self):
        data = self.audit['primary']['zero']
        for row in data['results'].values():
            self.assertEqual(row['original_return'], row['adjusted_return'])
        self.assertEqual(len(data['remaining_unfavorable']), 12)
        self.assertEqual(data['previously_unfavorable_fixed'], 0)

    def test_negative_candidates_fix_12_without_hiding_other_cases(self):
        for name in ('stationary_collapse', 'observed_collapse', 'physical_lower_bound'):
            data = self.audit['primary'][name]
            self.assertEqual(data['previously_unfavorable_fixed'], 12)
            self.assertEqual(len(data['comparisons']), 63)
            self.assertEqual(data['remaining_unfavorable'], [])

    def test_tail_only_applies_before_horizon_to_confirmed_fall(self):
        for data in self.audit['primary'].values():
            for row in data['results'].values():
                if not row['terminated'] or row['terminal_step'] == 1200:
                    self.assertEqual(row['difference'], 0)
                    self.assertEqual(row['remaining_virtual_ticks'], 0)
        for name in ('cap_fallen', 'cap_unbalanced'):
            self.assertEqual(self.audit['primary']['physical_lower_bound']['results'][name]['difference'], 0)

    def test_analytic_adjustment_equals_explicit_virtual_tail_for_all_real_states(self):
        name = 'severe_fall_wait0'
        with (ROOT / 'docs/traces/step129-incentives' / f'{name}.csv').open() as file:
            rewards = [float(row['reward']) for row in csv.DictReader(file)]
        data = self.audit['primary']['physical_lower_bound']['results'][name]
        utility = self.audit['candidates']['physical_lower_bound']['value']
        virtual = rewards + [utility] * data['remaining_virtual_ticks']
        expected = discounted_returns(virtual)
        corrected = rewards[:-1] + [rewards[-1] + data['terminal_relative_tail']]
        actual = discounted_returns(corrected)
        self.assertEqual(len(corrected), len(rewards))
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, places=10)
        self.assertAlmostEqual(actual[0], data['adjusted_return'], places=10)

    def test_every_result_is_finite_and_negative_utility_never_adds_reward(self):
        for data in self.audit['primary'].values():
            for row in data['results'].values():
                for key in ('original_return', 'adjusted_return', 'difference', 'terminal_relative_tail'):
                    self.assertTrue(math.isfinite(row[key]))
                self.assertLessEqual(row['adjusted_return'], row['original_return'])

    def test_lower_bound_candidate_across_gamma_and_horizon_sensitivity(self):
        rows = [r for r in self.audit['sensitivity'] if r['candidate'] == 'physical_lower_bound']
        self.assertEqual(len(rows), 16)
        for row in rows:
            self.assertEqual(row['still_favoring_quick_fall'], [])

    def test_full_diagnostic_replay_is_deterministic(self):
        saved = json.loads((ROOT / 'docs/traces/step130-absorbing-failure/summary.json').read_text())
        for key in ('candidates', 'primary', 'sensitivity', 'original_metadata'):
            self.assertEqual(self.audit[key], saved[key])

    def test_reference_levels_are_derived_not_fitted_to_recovery_rankings(self):
        from python.balance_reward_v2 import RewardV2Config
        config = RewardV2Config();
        c = self.audit['candidates']
        self.assertEqual(c['stationary_collapse']['value'], -config.posture_weight - config.height_weight)
        self.assertEqual(c['physical_lower_bound']['value'],
                         -config.posture_weight - config.height_weight - config.motion_weight)
        self.assertGreater(c['observed_collapse']['samples'], 0)
        self.assertGreaterEqual(c['observed_collapse']['value'], c['physical_lower_bound']['value'])


if __name__ == '__main__':
    unittest.main()
