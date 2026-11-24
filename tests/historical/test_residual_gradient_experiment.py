"""Paired frozen-network gradients retain latent samples and production scaling."""
import torch
import unittest
from unittest.mock import patch

from python.aura_policy import AuraPolicy, AuraValue
from python.experiments.residual_gradient_experiment import (
    collect_gradient_rollouts, paired_policy_gradients, summarize_gradients, run_residual_gradient_experiment,
)
from python.train_nn import collect_critic_rollouts


class TinyEnvironment:
    observation_size = 205
    episode_limit = 10

    def __init__(self):
        self.actions = []

    def reset(self):
        self.steps = 0
        return torch.zeros(205), {}

    def step(self, action):
        assert not torch.is_grad_enabled()
        self.actions.append(action)
        self.steps += 1
        return torch.full((205,), self.steps / 10), 1. + action['left_hip_target'], self.steps == 2, False, {
            'steps': self.steps}


class ResidualGradientTests(unittest.TestCase):
    def models(self):
        with torch.random.fork_rng(devices=[]):
            torch.manual_seed(42)
            actor = AuraPolicy(205).eval().requires_grad_(False)
            critic = AuraValue(205).eval().requires_grad_(False)
        return actor, critic

    def test_collector_exactly_matches_canonical_sampling_without_graphs(self):
        actor, _ = self.models()
        first, second = TinyEnvironment(), TinyEnvironment()
        data = collect_gradient_rollouts(first, actor, 3, 11601)
        canonical = collect_critic_rollouts(second, actor, 3, 11601)
        self.assertEqual(first.actions, second.actions)
        self.assertEqual(data['returns_by_episode'], canonical['returns_by_episode'])
        self.assertEqual(data['episode_scores'], canonical['episode_scores'])
        for left, right in zip(data['states_by_episode'], canonical['states_by_episode']):
            for a, b in zip(left, right): torch.testing.assert_close(a, b, rtol=0, atol=0)
        self.assertTrue(all(not value.requires_grad for ep in data['latent_actions_by_episode'] for value in ep))
        self.assertTrue(all(p.grad is None and not p.requires_grad for p in actor.parameters()))

    def test_manual_gradient_uses_original_latents_and_same_actor_scale(self):
        actor, critic = self.models()
        with torch.no_grad():
            for model in (actor, critic):
                for p in model.parameters(): p.zero_()
            critic.network[-1].bias.fill_(.1)
        count = len(actor.action_transforms)
        data = {'states_by_episode': [[torch.zeros(205), torch.zeros(205)], [torch.zeros(205)]],
                'returns_by_episode': [[100., 20.], [50.]],
                'latent_actions_by_episode': [[torch.full((count,), 2.), torch.full((count,), -1.5)],
                                              [torch.full((count,), .25)]]}
        before = [[p.clone() for p in model.parameters()] for model in (actor, critic)]
        vectors, details = paired_policy_gradients(actor, critic, data, [.4, .1], .3, 10)
        # For zero means, d log N(z;mu,.25)/d mu = z/.25^2.
        # Advantages A=[6,1,1], B=[5,0,0]; sum / two episodes.
        torch.testing.assert_close(vectors['timestep'][-count:], torch.full((count,), -86.), rtol=1e-6, atol=1e-5)
        torch.testing.assert_close(vectors['residual'][-count:], torch.full((count,), -80.), rtol=1e-6, atol=1e-5)
        self.assertEqual(details['clipped_latent_coordinates'], 2 * count)
        for model, snapshot in zip((actor, critic), before):
            for p, old in zip(model.parameters(), snapshot):
                torch.testing.assert_close(p, old, rtol=0, atol=0)
                self.assertFalse(p.requires_grad)
                self.assertIsNone(p.grad)
        with torch.no_grad():
            critic.network[-1].bias.zero_()
        equal, _ = paired_policy_gradients(actor, critic, data, [.4, .1], .3, 10)
        torch.testing.assert_close(equal['timestep'], equal['residual'], rtol=0, atol=0)
        data['latent_actions_by_episode'][0].pop()
        with self.assertRaisesRegex(ValueError, 'original latent'):
            paired_policy_gradients(actor, critic, data, [.4, .1], .3, 10)

    def test_variance_is_unbiased_per_coordinate_and_covariance_trace(self):
        vectors = {'timestep': [torch.tensor([1., 0.]), torch.tensor([3., 0.]), torch.tensor([5., 0.])],
                   'residual': [torch.tensor([1., 0.]), torch.tensor([2., 0.]), torch.tensor([3., 0.])]}
        summary, variance = summarize_gradients(vectors,
                                                [{'name': 'weight', 'shape': [2], 'count': 2, 'start': 0, 'end': 2}])
        self.assertEqual(variance['timestep'].tolist(), [4., 0.])
        self.assertEqual(variance['residual'].tolist(), [1., 0.])
        self.assertEqual(summary['variance_reduction_percent'], 75.)
        self.assertEqual(summary['methods']['timestep']['mean_pairwise_cosine'], 1.)
        self.assertEqual(summary['coordinate_variance_comparison'], {'lower': 1, 'equal': 1, 'higher': 0})

    def test_five_paired_batches_do_not_step_or_fit_either_network(self):
        actor, critic = self.models()
        calls = []
        original = collect_gradient_rollouts

        def collect(*args, **kwargs):
            calls.append(args[3])
            return original(*args, **kwargs)

        with patch('python.experiments.residual_gradient_experiment.collect_gradient_rollouts', side_effect=collect), \
                patch('torch.optim.Adam', side_effect=AssertionError('No optimizer permitted')):
            report, artifact = run_residual_gradient_experiment(actor, critic, [.015, .005], .01,
                                                                env_factory=TinyEnvironment, batch_size=2)
        self.assertEqual(calls, [11601, 11602, 11603, 11604, 11605])
        self.assertEqual(report['actor_max_parameter_change'], 0)
        self.assertEqual(report['critic_max_parameter_change'], 0)
        self.assertEqual(report['actor_optimizer_updates'], 0)
        self.assertEqual(report['critic_optimizer_updates'], 0)
        self.assertEqual(len(report['methods']['timestep']['pairwise_cosines']), 10)
        self.assertEqual(len(artifact['gradients']['residual']), 5)
        for seeds in ((1, 1, 3, 4, 5), (11502, 2, 3, 4, 5), (1, 2, 3)):
            with self.assertRaisesRegex(ValueError, 'independent'):
                run_residual_gradient_experiment(actor, critic, [1.], 0., exploration_seeds=seeds)


if __name__ == '__main__': unittest.main()
