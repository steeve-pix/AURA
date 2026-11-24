"""Objective migration preserves saved actors without reusing old value targets."""
import hashlib
from pathlib import Path
import tempfile
import unittest
import torch
from cli.backend.training_run import TrainingRun
from python.arena_env import make_environment, checkpoint_environment
from python.model_checkpoint import load_checkpoint, save_checkpoint, validate_checkpoint_contract
from python.train_nn import TrainingConfig, create_training, create_critic
from cli.backend.model_viewer import model_catalog


class RewardRepairTests(unittest.TestCase):
    def source(self, directory):
        config = TrainingConfig(batch_size=4, agents=4)
        env, actor, optimizer = create_training(config, environment=make_environment(4, quality_version=2))
        critic, co = create_critic(env, config)
        # Make the historical critic observably different from a fresh one.
        with torch.no_grad():
            for parameter in critic.parameters(): parameter.add_(1.)
        path = Path(directory)/'historical_best.pt'
        save_checkpoint(actor, optimizer, 8, path, critic=critic, critic_optimizer=co)
        return path, config

    def test_history_replays_but_cannot_silently_resume(self):
        with tempfile.TemporaryDirectory() as directory:
            path, config = self.source(directory)
            data = torch.load(path, weights_only=True)
            env = checkpoint_environment(data)
            load_checkpoint(path, env)
            self.assertEqual(env.quality_version, 2)
            with self.assertRaisesRegex(ValueError, 'quality_version'):
                validate_checkpoint_contract(data, make_environment(4))
            rows = list(model_catalog(Path(directory)))
            self.assertTrue(any('\thistorical\t' in row for row in rows))

    def test_fork_keeps_exact_actor_and_source_but_resets_training_state(self):
        with tempfile.TemporaryDirectory() as directory:
            path, config = self.source(directory)
            before = path.read_bytes()
            data = torch.load(path, weights_only=True)
            with TrainingRun(config, 'fork', name='corrected', checkpoint=path, directory=directory) as run:
                self.assertEqual(run.env.quality_version, 3)
                self.assertEqual(run.episodes_completed, 0)
                self.assertEqual(run.updates_completed, 0)
                self.assertFalse(run.optimizer.state)
                self.assertFalse(run.critic_optimizer.state)
                self.assertTrue(all(torch.equal(run.policy.state_dict()[k], v) for k,v in data['model'].items()))
                self.assertTrue(any(not torch.equal(run.critic.state_dict()[k], v) for k,v in data['critic']['model'].items()))
                run.save_current()
                fork_path = run.path
            self.assertEqual(path.read_bytes(), before)
            fork = torch.load(fork_path, weights_only=True)
            self.assertEqual(fork['fork_source']['sha256'], hashlib.sha256(before).hexdigest())
            with TrainingRun(config, 'resume', checkpoint=fork_path, directory=directory) as run:
                run.save_current()
            self.assertEqual(torch.load(fork_path, weights_only=True)['fork_source'], fork['fork_source'])

    def test_fork_rejects_world_or_physics_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            path, config = self.source(directory)
            original=path.read_bytes()
            with TrainingRun(TrainingConfig(batch_size=4, agents=2), 'fork', checkpoint=path, directory=directory) as fork:
                self.assertEqual(fork.env.agents,2)
                self.assertTrue(fork.env.is_independent)
                self.assertEqual(path.read_bytes(),original)
            data = torch.load(path, weights_only=True)
            data['floor_friction_version'] = 999
            torch.save(data, path)
            with self.assertRaisesRegex(ValueError, 'floor_friction_version'):
                TrainingRun(config, 'fork', checkpoint=path, directory=directory)

    def test_unknown_quality_cannot_fall_back(self):
        for version in (0, 4, True, '2'):
            with self.assertRaises(ValueError): make_environment(quality_version=version)


if __name__ == '__main__': unittest.main()
