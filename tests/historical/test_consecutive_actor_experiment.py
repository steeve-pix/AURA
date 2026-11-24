"""The pilot uses separate fitting experience and persistent optimizer states."""
import json
import tempfile
import torch
import unittest
from pathlib import Path
from unittest.mock import patch

from python.experiments.actor_update_experiment import snapshot, maximum_change
from python.aura_policy import AuraPolicy, AuraValue
from python.experiments.consecutive_actor_experiment import actor_gradient, step_actor, run_seed, streams, evaluation_seeds
from python.experiments.residual_gradient_experiment import collect_gradient_rollouts, paired_policy_gradients


class TinyEnvironment:
    observation_size = 205
    episode_limit = 1200

    def reset(self):
        self.steps = 0
        return torch.zeros(205), {'done': False, 'steps': 0}

    def step(self, action):
        self.steps += 1
        return torch.zeros(205), 1. + action['left_hip_target'], self.steps == 2, False, {'done': self.steps == 2,
                                                                                          'steps': self.steps}


def models():
    with torch.random.fork_rng(devices=[]):
        torch.manual_seed(42)
        actor = AuraPolicy(205).eval().requires_grad_(False)
        critic = AuraValue(205).eval().requires_grad_(False)
    return actor, critic


class ConsecutiveActorTests(unittest.TestCase):
    def test_gradients_match_validated_one_update_formulas(self):
        actor, critic = models()
        data = collect_gradient_rollouts(TinyEnvironment(), actor, 2, 121042011)
        table, constant = [.1, .05], .08
        expected, _ = paired_policy_gradients(actor, critic, data, table, constant, 1200)
        for method in ('timestep', 'residual'):
            actual, _ = actor_gradient(actor, data, table, constant, 1200, critic if method == 'residual' else None)
            torch.testing.assert_close(actual, expected[method], rtol=0, atol=0)
        self.assertTrue(all(p.grad is None for model in (actor, critic) for p in model.parameters()))

    def test_actor_adam_moments_persist_across_three_steps(self):
        actor, _ = models()
        optimizer = torch.optim.Adam(actor.parameters(), lr=.001)
        size = sum(p.numel() for p in actor.parameters())
        for update, sign in enumerate((1., -1., 1.), 1):
            step_actor(actor, optimizer, torch.full((size,), sign), update)
            self.assertTrue(all(int(s['step']) == update for s in optimizer.state.values()))
        self.assertTrue(all(p.grad is None and not p.requires_grad for p in actor.parameters()))

    def test_fit_gradient_and_evaluation_streams_are_disjoint(self):
        all_training = []
        all_eval = []
        for seed in (42, 43, 44):
            all_eval.extend(evaluation_seeds(seed))
            for update in (1, 2, 3): all_training.extend(streams(seed, update).values())
        self.assertEqual(len(set(all_training)), 18)
        self.assertEqual(len(set(all_eval)), 192)
        self.assertFalse(set(all_training) & set(all_eval))

    def test_complete_pilot_keeps_checkpoints_and_frozen_reference(self):
        actor, critic = models()
        before = snapshot(actor)
        with tempfile.TemporaryDirectory() as directory, \
                patch('python.experiments.consecutive_actor_experiment.AuraEnv', TinyEnvironment), \
                patch('python.experiments.stochastic_policy_experiment.AuraEnv', TinyEnvironment), \
                patch('python.experiments.consecutive_actor_experiment.load_reference', return_value=(actor, critic, [], 0., {})), \
                patch('python.experiments.consecutive_actor_experiment.evaluate_stochastic') as evaluate:
            # Keep the real seeded evaluation implementation with a tiny environment.
            from python.experiments.stochastic_policy_experiment import evaluate_stochastic
            evaluate.side_effect = lambda model, seeds, progress: evaluate_stochastic(model, seeds,
                                                                                      env_factory=TinyEnvironment)
            result = run_seed(42, directory, 'unused')
            self.assertEqual(maximum_change(actor, before), 0)
            self.assertEqual(result['critic_optimizer_steps'], 60)
            self.assertEqual(evaluate.call_count, 7)
            for method in ('timestep', 'residual'):
                root = Path(directory) / 'seed-42' / method
                latest = torch.load(root / 'actor.pt', weights_only=True)
                self.assertEqual(latest['updates_completed'], 3)
                self.assertEqual(latest['episodes_completed'], 96)
                best = torch.load(root / 'actor_best.pt', weights_only=True)
                scores = [e['stochastic_discounted_return']['mean'] for e in result['records'][method]]
                self.assertEqual(best['best_evaluation_discounted_return'], max(scores))
                for update in range(4): self.assertTrue((root / f'actor-update-{update}.pt').exists())
            for round in result['rounds']:
                self.assertNotEqual(round['streams']['fit'], round['streams']['gradient'])
                self.assertTrue(round['methods']['residual']['critic_frozen_during_actor_update'])
                self.assertEqual(round['methods']['residual']['fit']['critic_optimizer_steps'], round['update'] * 20)


if __name__ == '__main__': unittest.main()
