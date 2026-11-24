"""Controlled actor updates must preserve references and use fresh Adam states."""
import tempfile
import torch
import unittest
from pathlib import Path
from unittest.mock import patch

from python.experiments.actor_update_experiment import fresh_pair, apply_one_update, snapshot, maximum_change, run_comparison
from python.aura_policy import AuraPolicy, AuraValue


class ActorUpdateTests(unittest.TestCase):
    def test_equal_gradients_produce_equal_single_updates(self):
        actor = AuraPolicy(205).eval().requires_grad_(False)
        before = snapshot(actor)
        actors, optimizers = fresh_pair(actor)
        gradient = torch.linspace(-1, 1, sum(p.numel() for p in actor.parameters()))
        for method in actors:
            self.assertEqual(len(optimizers[method].state), 0)
            apply_one_update(actors[method], optimizers[method], gradient)
            self.assertTrue(all(state['step'].item() == 1 for state in optimizers[method].state.values()))
            self.assertTrue(all(p.grad is None for p in actors[method].parameters()))
        self.assertEqual(maximum_change(actors['residual'], snapshot(actors['timestep'])), 0)
        self.assertGreater(maximum_change(actors['residual'], before), 0)
        self.assertEqual(maximum_change(actor, before), 0)
        with self.assertRaises(ValueError):
            apply_one_update(actors['residual'], optimizers['residual'], gradient)

    def test_shared_experience_and_initial_best_survives_worse_update(self):
        actor, critic = AuraPolicy(205), AuraValue(205)
        critic_before = snapshot(critic)
        gradient = torch.ones(sum(p.numel() for p in actor.parameters()))
        data = {'shared': True}

        class Env:
            episode_limit = 1200
            observation_size = 205

        with tempfile.TemporaryDirectory() as directory, \
                patch('python.experiments.actor_update_experiment.collect_gradient_rollouts', return_value=data) as collect, \
                patch('python.experiments.actor_update_experiment.paired_policy_gradients', return_value=(
                        {'timestep': gradient, 'residual': -gradient}, {})) as paired, \
                patch('python.experiments.actor_update_experiment.evaluate_policy', side_effect=[100, 90, 90, 100, 110, 110, 110]):
            report = run_comparison(actor, critic, [], 0, directory, env_factory=Env)
            self.assertEqual(collect.call_count, 1)
            self.assertIs(paired.call_args.args[2], data)
            self.assertEqual(collect.call_args.args[2], 32)
            self.assertEqual(report['critic_maximum_parameter_change'], 0)
            self.assertEqual(maximum_change(critic, critic_before), 0)
            for method, score, best, updates in [('timestep', 90, 100, 0), ('residual', 110, 110, 1)]:
                row = report['methods'][method]
                self.assertEqual(row['survival_after_update_steps'], score)
                self.assertEqual(row['best_survival_steps'], best)
                self.assertEqual(row['best_updates_completed'], updates)
                saved = torch.load(Path(directory) / method / 'actor.pt', weights_only=True)
                self.assertEqual(saved['updates_completed'], 1)
                self.assertEqual(saved['episodes_completed'], 32)
            self.assertTrue(all(p.grad is None for p in critic.parameters()))

    def test_five_batches_reset_reference_and_preserve_paired_outcomes(self):
        from python.experiments.actor_update_experiment import run_five_batches
        actor, critic = AuraPolicy(205), AuraValue(205)
        before = snapshot(actor)

        def result(timestep, residual):
            return {'methods': {method: {
                'survival_after_update_steps': score,
                'change_from_initial_steps': score - 330,
            } for method, score in [('timestep', timestep), ('residual', residual)]}}

        reused = result(330, 333)
        seeds = []

        def comparison(given_actor, given_critic, table, constant, directory, **kwargs):
            self.assertIs(given_actor, actor)
            self.assertIs(given_critic, critic)
            self.assertEqual(maximum_change(given_actor, before), 0)
            copies, optimizers = fresh_pair(given_actor)
            self.assertTrue(all(not optimizer.state for optimizer in optimizers.values()))
            self.assertTrue(all(maximum_change(model, before) == 0 for model in copies.values()))
            seeds.append(kwargs['exploration_seed'])
            return result(331, 329)

        with tempfile.TemporaryDirectory() as directory, patch(
                'python.experiments.actor_update_experiment.run_comparison', side_effect=comparison) as run:
            report = run_five_batches(actor, critic, [], 0, directory, reused)
            self.assertEqual(run.call_count, 4)
        self.assertEqual(seeds, [11802, 11803, 11804, 11805])
        self.assertEqual([batch['batch'] for batch in report['batches']], list('ABCDE'))
        summary = report['summary']
        self.assertEqual(summary['residual_wins'], 1)
        self.assertEqual(summary['timestep_wins'], 4)
        self.assertEqual(summary['ties'], 0)
        self.assertEqual(summary['methods']['residual']['mean_survival_improvement_steps'], -0.2)
        self.assertEqual(summary['methods']['timestep']['mean_survival_improvement_steps'], 0.8)
        self.assertEqual(summary['paired_critic_minus_timestep_steps'], [3, -2, -2, -2, -2])


if __name__ == '__main__':
    unittest.main()
