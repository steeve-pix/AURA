"""Resume must continue learning exactly; new/restarted models must stay separate."""
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import torch
from cli.training_run import TrainingRun
from python.aura_policy import AuraPolicy
from python.train_nn import TrainingConfig, train_batch


def environment():
    from test_nn_configuration import ShortEnvironment
    env = ShortEnvironment()
    from python.aura_policy import ACTION_TRANSFORMS
    env.simulation = SimpleNamespace(action=lambda: dict.fromkeys(ACTION_TRANSFORMS, 0.0))
    return env


def create(config):
    torch.manual_seed(config.seed)
    env = environment()
    policy = AuraPolicy(env.observation_size, (9, 5), exploration_std=.4)
    return env, policy, torch.optim.Adam(policy.parameters(), lr=config.learning_rate)


class RunTests(unittest.TestCase):
    def setUp(self):
        factory = patch("cli.training_run.AuraEnv", environment)
        factory.start()
        self.addCleanup(factory.stop)

    def test_resume_matches_uninterrupted_training_and_counts(self):
        config = TrainingConfig(batch_size=3, seed=13, learning_rate=.004)
        with tempfile.TemporaryDirectory() as directory, patch("cli.training_run.create_training", create):
            with TrainingRun(config, name="Balance", directory=directory) as first:
                result = train_batch(first.env, first.policy, first.optimizer, config.batch_size)
                path = first.save(len(result["episode_scores"]))
                data = torch.load(path, weights_only=True)
                self.assertEqual(data["episodes_completed"], 3)
                self.assertEqual(data["updates_completed"], 1)
                run_id = data["training_run_id"]
                expected = train_batch(first.env, first.policy, first.optimizer, config.batch_size)
                weights = [p.detach().clone() for p in first.policy.parameters()]
            with TrainingRun(TrainingConfig(batch_size=3), "resume", checkpoint=path, directory=directory) as resumed:
                self.assertEqual(resumed.config.seed, config.seed)
                self.assertEqual(resumed.optimizer.param_groups[0]["lr"], config.learning_rate)
                self.assertEqual(resumed.updates_completed, 1)
                self.assertEqual(resumed.run_id, run_id)
                result = train_batch(resumed.env, resumed.policy, resumed.optimizer, 3)
                self.assertEqual(
                    {key: value for key, value in result.items() if key != "states_by_episode"},
                    {key: value for key, value in expected.items() if key != "states_by_episode"},
                )
                for actual_episode, expected_episode in zip(
                    result["states_by_episode"], expected["states_by_episode"]
                ):
                    for actual_state, expected_state in zip(actual_episode, expected_episode):
                        torch.testing.assert_close(actual_state, expected_state)
                for actual, wanted in zip(resumed.policy.parameters(), weights):
                    self.assertTrue(torch.equal(actual, wanted))
                resumed.save(len(result["episode_scores"]))
                data = torch.load(path, weights_only=True)
                self.assertEqual(data["episodes_completed"], 6)
                self.assertEqual(data["updates_completed"], 2)

    def test_unique_names_and_exclusive_training_lock(self):
        config = TrainingConfig(batch_size=3)
        with tempfile.TemporaryDirectory() as directory, patch("cli.training_run.create_training", create):
            with TrainingRun(config, name="Balance", directory=directory) as first:
                # A second active new run gets a different name even before either saves.
                with TrainingRun(config, name="balance", directory=directory) as second:
                    self.assertNotEqual(first.path, second.path)
                    first.save(0)
                    second.save(0)
                original = first.path.read_bytes()
                with self.assertRaisesRegex(ValueError, "already being trained"):
                    TrainingRun(config, "resume", checkpoint=first.path, directory=directory)
                with TrainingRun(config, name="BALANCE", directory=directory) as third:
                    self.assertEqual(third.path.name, "balance_3.pt")
                    self.assertEqual(first.path.read_bytes(), original)
            with TrainingRun(config, "resume", checkpoint=first.path, directory=directory):
                pass  # Closing the previous session releases its lock.

    def test_restart_is_fresh_and_preserves_previous_save_until_new_batch(self):
        config = TrainingConfig(batch_size=3)
        with tempfile.TemporaryDirectory() as directory, patch("cli.training_run.create_training", create):
            with TrainingRun(config, directory=directory) as run:
                train_batch(run.env, run.policy, run.optimizer, 3)
                path = run.save(3)
            original = path.read_bytes()
            original_id = torch.load(path, weights_only=True)["training_run_id"]
            with TrainingRun(config, "restart", checkpoint=path, directory=directory) as run:
                self.assertNotEqual(run.run_id, original_id)
                self.assertEqual(run.episodes_completed, 0)
                self.assertEqual(run.updates_completed, 0)
                self.assertFalse(run.optimizer.state)
                self.assertEqual(path.read_bytes(), original)
                train_batch(run.env, run.policy, run.optimizer, 3)
                run.save(3)
                self.assertEqual(torch.load(path, weights_only=True)["episodes_completed"], 3)
            for name in ("../balance", "", "a.pt", "with spaces"):
                with self.assertRaises(ValueError):
                    TrainingRun(config, name=name, directory=directory)


if __name__ == "__main__":
    unittest.main()
