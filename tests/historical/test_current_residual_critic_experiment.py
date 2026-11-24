"""The Step 115 test set stays unseen until the fixed final critic is frozen."""
import torch
import unittest
from unittest.mock import patch

from python.experiments.current_residual_critic_experiment import run_current_experiment, stage_metrics, CONFIG
from python.experiments.residual_critic_experiment import evaluate_residual
from python.train_nn import collect_critic_rollouts
from tests.support.short_environment import ShortEnvironment


class TinyEnvironment(ShortEnvironment):
    observation_size = 205

    def __init__(self, multiplier=1.):
        super().__init__()
        self.multiplier = multiplier

    def step(self, action):
        assert not torch.is_grad_enabled()
        state, reward, terminated, truncated, info = super().step(action)
        return state, reward * self.multiplier, terminated, truncated, info


class CurrentResidualCriticTests(unittest.TestCase):
    def test_test_is_collected_after_20_updates_and_cannot_change_final_weights(self):
        def experiment(test_multiplier):
            updates = []
            created = []
            final_evaluations = []

            def factory():
                env = TinyEnvironment(test_multiplier if created else 1.)
                created.append(env)
                return env

            def collect(env, actor, episodes, seed, *args, **kwargs):
                self.assertTrue(all(not p.requires_grad and p.grad is None for p in actor.parameters()))
                if seed == CONFIG['test_exploration_seed']:
                    self.assertEqual(updates, [0, 1, 5, 10, 20])
                    self.assertEqual(len(created), 2)
                else:
                    self.assertEqual(seed, CONFIG['training_exploration_seed'])
                    self.assertEqual(len(created), 1)
                return collect_critic_rollouts(env, actor, episodes, seed, *args, **kwargs)

            def evaluate(critic, *args, **kwargs):
                if all(not p.requires_grad for p in critic.parameters()): final_evaluations.append(True)
                return evaluate_residual(critic, *args, **kwargs)

            with patch('python.experiments.current_residual_critic_experiment.collect_critic_rollouts', side_effect=collect), \
                    patch('python.experiments.current_residual_critic_experiment.evaluate_residual', side_effect=evaluate):
                result = run_current_experiment(env_factory=factory,
                                                measurement=lambda row: updates.append(row['update']))
            self.assertEqual(len(final_evaluations), 1)
            return result

        report, critic, training, test, actor = experiment(1.)
        alternate, other, *_ = experiment(100.)
        self.assertEqual(report['actor_max_parameter_change'], 0)
        self.assertTrue(report['actor_has_no_gradients'])
        self.assertEqual(report['actor_optimizer_updates'], 0)
        self.assertEqual(report['critic_updates'], 20)
        self.assertGreater(report['critic_max_parameter_change'], 0)
        self.assertEqual(len(training['states_by_episode']), 32)
        self.assertEqual(len(test['states_by_episode']), 16)
        self.assertTrue(all(not state.requires_grad for episode in training['states_by_episode'] for state in episode))
        self.assertEqual(report['timestep_scaled_predictions'], alternate['timestep_scaled_predictions'])
        self.assertTrue(all(torch.equal(value, other['model'][key]) for key, value in critic['model'].items()))
        self.assertNotEqual(report['combined_test_mse'], alternate['combined_test_mse'])
        self.assertEqual(report['test_fallback_count'], 0)
        self.assertEqual(actor['seed'], 42)
        self.assertEqual(sum(row['count'] for row in report['test_stages']), report['test_timesteps'])

    def test_stage_errors_are_observation_weighted_and_empty_stages_stay_empty(self):
        bins = [{'count': 0, 'timestep_mse': None, 'combined_mse': None} for _ in range(6)]
        bins[0] = {'count': 2, 'timestep_mse': 1., 'combined_mse': .5}
        bins[1] = {'count': 1, 'timestep_mse': 4., 'combined_mse': 2.}
        stages = stage_metrics(bins)
        self.assertEqual(stages[0]['count'], 3)
        self.assertEqual(stages[0]['timestep_mse'], 2.)
        self.assertEqual(stages[0]['combined_mse'], 1.)
        self.assertIsNone(stages[2]['combined_mse'])


if __name__ == '__main__': unittest.main()
