import unittest

import torch

from python.train_nn import train_batch, DISCOUNT_FACTOR


class Policy(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.weight = torch.nn.Parameter(torch.tensor(1.0))

    def sample_action(self, state):
        return {}, self.weight * state.sum()


class Environment:
    episode_limit = 10

    def __init__(self):
        self.episode = -1
        self.calls = 0

    def reset(self):
        self.episode += 1
        self.index = 0
        return torch.tensor([1.0 if self.episode == 0 else 3.0]), {}

    def step(self, action):
        self.calls += 1
        rewards = ([1.0, 2.0], [3.0])[self.episode]
        reward = rewards[self.index]
        self.index += 1
        return torch.tensor([2.0]), reward, self.index == len(rewards), False, {"steps": self.index}


class GradientComparisonTests(unittest.TestCase):
    def test_experiment_uses_independent_streams_and_never_updates_actor(self):
        from python.gradient_experiment import run_gradient_experiment

        class ShortEnvironment:
            observation_size = 205
            episode_limit = 10

            def reset(self):
                self.index = 0
                return torch.zeros(205), {}

            def step(self, action):
                self.index += 1
                reward = 1.0 + action['left_hip_target']
                return torch.full((205,), self.index / 10), reward, self.index == 2, False, {'steps': self.index}

        report = run_gradient_experiment(env_factory=ShortEnvironment, batch_size=2)
        self.assertEqual(report['actor_optimizer_updates'], 0)
        self.assertEqual(report['actor_max_parameter_change'], 0)
        self.assertEqual(report['critic_max_parameter_change_during_comparison'], 0)
        self.assertTrue(report['same_trajectories_for_both_baselines'])
        self.assertEqual([row['exploration_seed'] for row in report['batches']], [4211, 4212, 4213])
        self.assertEqual(len({row['original_gradient_sha256'] for row in report['batches']}), 3)
        for similarities in report['cosine_similarities'].values():
            self.assertTrue(all(-1 <= value <= 1 for value in similarities.values()))
        with self.assertRaisesRegex(ValueError, 'independent'):
            run_gradient_experiment(exploration_seeds=(1, 1, 2))
        with self.assertRaisesRegex(ValueError, 'independent'):
            run_gradient_experiment(exploration_seeds=(4201, 3, 4))

    def test_paired_gradient_matches_manual_scale_and_preserves_original(self):
        critic = torch.nn.Linear(1, 1)
        with torch.no_grad():
            critic.weight.zero_()
            critic.bias.fill_(0.01)  # A raw-return baseline of 1, not .01.
        before = [p.clone() for p in critic.parameters()]
        results = []
        for compare in (None, critic):
            actor, env = Policy(), Environment()
            optimizer = torch.optim.Adam(actor.parameters())
            result = train_batch(env, actor, optimizer, 2, update_parameters=False, compare_critic=compare)
            self.assertEqual(env.calls, 3)  # No second collection or resampling.
            self.assertEqual(actor.weight.item(), 1.0)
            self.assertEqual(optimizer.state, {})
            results.append((result, actor.weight.grad.clone()))
        plain, paired = results
        self.assertEqual(plain[0]['loss'], paired[0]['loss'])
        torch.testing.assert_close(plain[1], paired[1], rtol=0, atol=0)
        expected = -(((1 + DISCOUNT_FACTOR * 2 - 1)/10) + 2*((2-1)/10) + 3*((3-1)/10))/2
        comparison = paired[0]['gradient_comparison']
        self.assertAlmostEqual(comparison['critic_gradient'].item(), expected, places=6)
        self.assertAlmostEqual(comparison['critic_loss'], expected, places=6)
        torch.testing.assert_close(comparison['original_gradient'], plain[1].reshape(1), rtol=0, atol=0)
        self.assertTrue(critic.training)  # Mode restored.
        for parameter, old in zip(critic.parameters(), before):
            torch.testing.assert_close(parameter, old, rtol=0, atol=0)
            self.assertIsNone(parameter.grad)

    def test_production_update_cannot_use_diagnostic_critic(self):
        actor = Policy()
        with self.assertRaisesRegex(ValueError, 'diagnostic only'):
            train_batch(Environment(), actor, torch.optim.Adam(actor.parameters()), compare_critic=torch.nn.Linear(1, 1))

    def test_nonfinite_or_wrong_shape_critic_rejected(self):
        for critic in (torch.nn.Linear(1, 2), torch.nn.Linear(1, 1)):
            if critic.out_features == 1:
                with torch.no_grad():
                    critic.bias.fill_(float('nan'))
            actor = Policy()
            with self.assertRaises(ValueError):
                train_batch(Environment(), actor, torch.optim.Adam(actor.parameters()), 2,
                            update_parameters=False, compare_critic=critic)


if __name__ == '__main__':
    unittest.main()
