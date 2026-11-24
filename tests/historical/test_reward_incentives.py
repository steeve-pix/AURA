"""Freeze Step 129 findings, including known bad incentives, without tuning reward.

These are objective regression tests, not assertions of learned recoverability.
Run after building aura_incentive_fixture_classifier.
"""
import csv
import math
import unittest
from pathlib import Path

from python.balance_reward_v2 import RewardV2Config
from python.diagnostics.reward_incentive_audit import ROOT, run_audit


class IncentiveAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.audit = run_audit(ROOT / 'docs/traces/step128-physical',
                              ROOT / 'build-release/aura_incentive_fixture_classifier')
        cls.results = cls.audit['results']

    def value(self, name):
        return self.results[name]['discounted_return']

    def test_shared_physical_and_reward_memory_at_forks(self):
        for cohort in ('mild', 'severe', 'very_severe', 'native_fork80', 'cap_boundary'):
            names = [n for n, m in self.audit['metadata'].items() if m['cohort'] == cohort]
            self.assertEqual(len({self.results[n]['initial_context_sha256'] for n in names}), 1)

    def test_discount_is_actual_training_convention(self):
        self.assertEqual(self.audit['gamma'], .995)
        for result in self.results.values():
            self.assertAlmostEqual(result['discounted_return'], sum(result['discounted_components'].values()),
                                   places=10)

    def test_native_flags_and_rewards_exactly_reproduce_step128(self):
        self.assertTrue(self.audit['native_classifier_replay_exact'])
        for name, metadata in self.audit['metadata'].items():
            if metadata['kind'] != 'integrated_physics_replay':
                continue
            path = ROOT / 'docs/traces/step128-rewards' / (name.removeprefix('physical_') + '.csv')
            with path.open() as file:
                expected = [float(r['reward_total']) for r in csv.DictReader(file)
                            if int(r['step']) > metadata['fork_step']]
            self.assertEqual(expected, [r['reward'] for r in self.results[name]['records']])

    def test_quick_failure_beats_delay_in_negative_instability_fixture(self):
        # A known deficiency that a future reward revision must explicitly change.
        fast = self.value('severe_fall_wait0')
        self.assertGreater(fast, self.value('severe_fall_wait240'))
        self.assertGreater(fast, self.value('severe_unbalanced_truncation'))

    def test_prompt_constructed_recovery_is_profitable(self):
        for ramp in (24, 60, 120):
            self.assertGreater(self.value(f'severe_recover_wait0_ramp{ramp}'), self.value('severe_fall_wait0'))
        values = [self.value(f'severe_recover_wait{wait}_ramp60') for wait in (0, 240, 720, 900)]
        self.assertEqual(values, sorted(values, reverse=True))

    def test_late_success_can_lose_despite_positive_undiscounted_reward(self):
        result = self.results['severe_recover_wait900_ramp60']
        self.assertGreater(result['total_reward'], 0)
        self.assertGreater(result['by_state']['BALANCED']['steps'], 0)
        self.assertFalse(result['terminated'])
        self.assertLess(result['discounted_return'], self.value('severe_fall_wait0'))
        self.assertGreater(sum(not r['recovery_wins'] for r in self.audit['recovery_comparisons']), 0)

    def test_positive_unbalanced_survival_is_not_equivalent_to_balanced(self):
        value = self.value('mild_unbalanced_truncation')
        self.assertGreater(value, self.value('mild_fall_wait0'))
        self.assertLess(value, self.value('mild_ideal_recovery_reference'))
        self.assertEqual(self.results['mild_unbalanced_truncation']['by_state']['BALANCED']['steps'], 0)

    def test_native_fast_fall_is_not_favored_over_all_physical_alternatives(self):
        quick = self.value('physical_rapid_rotation')
        for name in ('physical_neutral', 'physical_brief_airborne', 'physical_recovery_attempt'):
            self.assertGreater(self.value(name), quick)

    def test_actual_classifier_cap_and_terminal_penalty_semantics(self):
        fall = self.results['cap_fallen'];
        survive = self.results['cap_unbalanced']
        self.assertTrue(fall['terminated']);
        self.assertTrue(fall['truncated'])
        self.assertFalse(survive['terminated']);
        self.assertTrue(survive['truncated'])
        self.assertEqual(fall['terminal_penalty_sum'], -RewardV2Config().terminal_weight)
        self.assertEqual(survive['terminal_penalty_sum'], 0)
        for row in self.results.values():
            self.assertEqual(row['terminal_penalty_sum'], -RewardV2Config().terminal_weight if row['terminated'] else 0)

    def test_shaping_cannot_fix_length_bias_for_equal_start_and_zero_end_potential(self):
        for cohort in ('mild', 'severe', 'very_severe', 'native_fork80', 'cap_boundary'):
            values = [self.results[n]['discounted_components']['recovery'] for n, m in self.audit['metadata'].items()
                      if m['cohort'] == cohort]
            for value in values:
                self.assertAlmostEqual(value, values[0], places=12)

    def test_expected_recovery_probability_is_not_assumed_to_be_one(self):
        for case in self.audit['recovery_attempt_lotteries']:
            p = case['minimum_success_probability']
            expected = p * case['success_return'] + (1 - p) * case['failure_return']
            self.assertAlmostEqual(expected, case['rapid_fall_return'], places=10)
        # Above one means even guaranteed success in this delayed fixture loses.
        self.assertGreater(self.audit['recovery_attempt_lotteries'][-1]['minimum_success_probability'], 1)

    def test_fixed_penalty_is_insufficient_and_sensitivity_is_not_a_weight_change(self):
        for quality in ('severe', 'very_severe'):
            sensitivity = self.audit['terminal_penalty_sensitivity'][quality]
            self.assertGreater(sensitivity['maximum_required_for_this_grid'], RewardV2Config().terminal_weight)
        self.assertEqual(RewardV2Config().terminal_weight, 1.)

    def test_extreme_numeric_states_remain_bounded(self):
        config = RewardV2Config()
        lower = -sum((config.posture_weight, config.height_weight, config.motion_weight,
                      config.recovery_weight, config.terminal_weight))
        upper = config.support_weight + config.recovery_weight * config.discount
        for row in self.audit['extremes']:
            self.assertTrue(math.isfinite(row['reward']))
            self.assertGreaterEqual(row['reward'], lower);
            self.assertLessEqual(row['reward'], upper)
        self.assertEqual(self.audit['extremes'][-1]['qualities']['motion'], 0)


if __name__ == '__main__':
    unittest.main()
