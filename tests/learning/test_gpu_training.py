"""GPU selection keeps CPU simulation inputs and actor/critic updates compatible."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import torch
from python.train_nn import TrainingConfig, create_critic, create_training, resolve_training_device, train_batch


class ShortEnvironment:
    observation_size = 17
    episode_limit = 10
    environment_contract = {"reward_version": 3}

    def __init__(self):
        self.episode = 0

    def reset(self):
        self.episode += 1
        self.steps = 0
        return torch.zeros(self.observation_size), {}

    def step(self, action):
        self.steps += 1
        done = self.steps >= 2
        return torch.zeros(self.observation_size), 1.0, done, False, {
            "steps": self.steps, "pelvis_height": 3.0, "pelvis_uprightness": 1.0,
            "balance_state": "BALANCED", "reward_qualities": {"balance_quality": 1.0},
            "balance_metrics": {}, "arena_contacts": 0,
        }


class DeviceTrainingTests(unittest.TestCase):
    def test_default_device_remains_cpu_and_invalid_choices_are_rejected(self):
        self.assertEqual(TrainingConfig().device, "cpu")
        with self.assertRaisesRegex(ValueError, "device"):
            TrainingConfig(device="metal")

    def test_gpu_selection_resolves_or_reports_unavailable_device(self):
        available = torch.cuda.is_available() or (
            hasattr(torch.backends, "mps") and torch.backends.mps.is_available())
        if available:
            self.assertIn(resolve_training_device("gpu").type, ("cuda", "mps"))
        else:
            with self.assertRaisesRegex(RuntimeError, "no supported"):
                resolve_training_device("gpu")

    @unittest.skipUnless(torch.cuda.is_available() or
                         (hasattr(torch.backends, "mps") and torch.backends.mps.is_available()),
                         "No CUDA or Apple Metal device is available")
    def test_actor_and_critic_update_on_gpu_from_cpu_simulation_observations(self):
        config = TrainingConfig(batch_size=2, device="gpu")
        env, actor, actor_optimizer = create_training(config, environment=ShortEnvironment())
        critic, critic_optimizer = create_critic(env, config)
        expected_device = resolve_training_device("gpu")
        self.assertEqual(next(actor.parameters()).device.type, expected_device.type)
        self.assertEqual(next(critic.parameters()).device.type, expected_device.type)
        actor_before = [parameter.detach().clone() for parameter in actor.parameters()]
        critic_before = [parameter.detach().clone() for parameter in critic.parameters()]
        result = train_batch(env, actor, actor_optimizer, 2, critic=critic,
                             critic_optimizer=critic_optimizer)
        self.assertTrue(torch.isfinite(torch.tensor(result["critic_loss"])))
        self.assertTrue(any(not torch.equal(before, after) for before, after in
                            zip(actor_before, actor.parameters())))
        self.assertTrue(any(not torch.equal(before, after) for before, after in
                            zip(critic_before, critic.parameters())))


if __name__ == "__main__":
    unittest.main()
