"""Held-out evaluation matches per-episode noise without updating policies."""
import math
import torch
import unittest
from unittest.mock import patch

from python.aura_policy import AuraPolicy
from python.experiments.stochastic_policy_experiment import evaluate_stochastic, paired_change, compare_results
from python.train_nn import DISCOUNT_FACTOR


class TinyEnvironment:
    episode_limit = 1200

    def __init__(self, horizon=2):
        self.horizon = horizon

    def reset(self):
        self.steps = 0
        return torch.zeros(205), {}

    def step(self, action):
        assert not torch.is_grad_enabled()
        self.steps += 1
        return torch.zeros(205), 2., self.steps == self.horizon, False, {'steps': self.steps}


class StochasticEvaluationTests(unittest.TestCase):
    def test_episode_streams_stay_matched_when_lengths_diverge(self):
        actor = AuraPolicy(205)
        before = {name: value.clone() for name, value in actor.state_dict().items()}
        rng = torch.get_rng_state().clone()
        with patch('torch.optim.Adam', side_effect=AssertionError('No evaluation optimizer')):
            short, short_data = evaluate_stochastic(actor, [119001, 119002], env_factory=TinyEnvironment)
            long, long_data = evaluate_stochastic(actor, [119001, 119002], env_factory=lambda: TinyEnvironment(3))
        for a, b in zip(short_data['latent_actions_by_episode'], long_data['latent_actions_by_episode']):
            for left, right in zip(a, b):
                self.assertTrue(torch.equal(left, right))
        self.assertTrue(torch.equal(rng, torch.get_rng_state()))
        self.assertEqual(short['stochastic_reward']['mean'], 4.)
        self.assertAlmostEqual(short['stochastic_discounted_return']['mean'], 2. + 2. * DISCOUNT_FACTOR)
        self.assertEqual(long['stochastic_survival_steps']['mean'], 3)
        self.assertTrue(all(torch.equal(value, actor.state_dict()[name]) for name, value in before.items()))
        self.assertTrue(all(p.grad is None for p in actor.parameters()))
        self.assertEqual(short['optimizer_updates'], 0)

    def test_rejects_training_and_duplicate_episode_seeds(self):
        actor = AuraPolicy(205)
        for seeds in ([11501, 119001], [119001, 119001], [119001]):
            with self.assertRaises(ValueError):
                evaluate_stochastic(actor, seeds, env_factory=TinyEnvironment)

    def test_paired_differences_and_batch_summary(self):
        seeds = list(range(119001, 119033))
        initial = {'episode_seeds': seeds, 'episode_rewards': [10.] * 32,
                   'episode_discounted_returns': [8.] * 32, 'episode_survival_steps': [330] * 32}
        result = {'episode_seeds': seeds, 'episode_rewards': [12.] * 32,
                  'episode_discounted_returns': [9.] * 32, 'episode_survival_steps': [320] * 32}
        delta = paired_change(result, initial, 'episode_rewards')
        self.assertEqual(delta['mean'], 2)
        self.assertEqual(delta['sample_standard_deviation'], 0)
        updated = {f'{label}-{method}': result for label in 'ABCDE' for method in ('timestep', 'residual')}
        batches = [
            {'batch': label, 'methods': {method: {'survival_after_update_steps': 320, 'change_from_initial_steps': -10}
                                         for method in ('timestep', 'residual')}} for label in 'ABCDE']
        rows, summary = compare_results(initial, updated, batches)
        self.assertEqual(len(rows), 5)
        self.assertEqual(summary['residual']['mean_reward_change'], 2)
        self.assertEqual(summary['residual']['mean_discounted_return_change'], 1)
        self.assertEqual(summary['residual']['batches_with_improved_reward'], 5)
        self.assertEqual(rows[0]['methods']['timestep']['deterministic_change_steps'], -10)
        changed = dict(result, episode_seeds=list(reversed(seeds)))
        with self.assertRaises(ValueError):
            paired_change(changed, initial, 'episode_rewards')


if __name__ == '__main__':
    unittest.main()
