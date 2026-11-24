"""Evaluation must be deterministic, read-only, and compare only matching protocols."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import torch
from cli.backend.evaluate_model import evaluate_checkpoint, previous_result, evaluation_protocol
from python.aura_policy import AuraPolicy, ACTION_TRANSFORMS
from python.model_checkpoint import save_checkpoint
from python.train_nn import evaluate_policy


class Environment:
    observation_size = 5
    episode_limit = 10
    environment_contract = dict(environment_version=1, termination_mode="legacy", termination_version=1,
                                reward_version=1, reward_name="legacy_balance_reward")

    def __init__(self):
        self.simulation = self
        self.steps = 0
        self.actions = []

    def action(self):
        return dict.fromkeys(ACTION_TRANSFORMS, 0.)

    def motor_states(self):
        return {"head": False, "neck": True}

    def observation(self):
        return {"steps": self.steps, "time": self.steps * .1, "done": self.steps >= 3,
                "termination_reason": "fallen" if self.steps >= 3 else "none",
                "balance_metrics": dict(steps=self.steps, balanced_steps=0, balanced_seconds=0., unbalanced_steps=self.steps,
                    longest_balanced_steps=0, recoveries=0, fall_rate=float(self.steps >= 3),
                    average_balance_quality=0., total_reward=0., termination_reason="fallen")}

    def reset(self):
        self.steps = 0
        return torch.zeros(self.observation_size), self.observation()

    def step(self, action):
        self.actions.append(action)
        self.steps += 1
        return torch.zeros(self.observation_size), 0., self.steps >= 3, False, self.observation()


class EvaluationTests(unittest.TestCase):
    def test_motor_configuration_changes_evaluation_protocol(self):
        env = Environment()
        before = evaluation_protocol(env)
        with patch.object(env, "motor_states", return_value={"head": True, "neck": False}):
            self.assertNotEqual(evaluation_protocol(env), before)

    def test_mode_gradients_and_weights_are_preserved(self):
        policy = AuraPolicy(Environment.observation_size, (7,))
        before = {key: value.clone() for key, value in policy.state_dict().items()}
        with patch.object(policy, "sample_action", side_effect=AssertionError("Exploration must not run")):
            for training in (True, False):
                policy.train(training)
                self.assertEqual(evaluate_policy(Environment(), policy), 3)
                self.assertEqual(policy.training, training)
        self.assertTrue(all(torch.equal(value, before[key]) for key, value in policy.state_dict().items()))
        self.assertTrue(all(parameter.grad is None for parameter in policy.parameters()))
        env = Environment()
        policy.train()
        with patch.object(env, "step", side_effect=RuntimeError("step failed")):
            with self.assertRaises(RuntimeError): evaluate_policy(env, policy)
        self.assertTrue(policy.training)

    def test_checkpoint_is_unchanged_and_previous_result_persists(self):
        policy = AuraPolicy(Environment.observation_size, (7,))
        optimizer = torch.optim.Adam(policy.parameters())
        with tempfile.TemporaryDirectory() as directory, patch("python.arena_env.checkpoint_environment", lambda _: Environment()):
            path = Path(directory) / "balance.pt"
            save_checkpoint(policy, optimizer, 8, path)
            original = path.read_bytes()
            first = evaluate_checkpoint(path)
            second = evaluate_checkpoint(path)
            self.assertEqual(first["steps"], 3)
            self.assertIsNone(first["previous_steps"])
            self.assertEqual(second["previous_steps"], 3)
            self.assertEqual(path.read_bytes(), original)
            history = path.parent / ".evaluations" / (path.name + ".json")
            changed = dict(second["protocol"], episode_limit=999)
            self.assertIsNone(previous_result(history, changed))
            saved = history.read_bytes()
            with patch("cli.backend.evaluate_model.EvaluationEnvironment.reset", side_effect=KeyboardInterrupt):
                with self.assertRaises(KeyboardInterrupt): evaluate_checkpoint(path)
            self.assertEqual(history.read_bytes(), saved)


if __name__ == "__main__": unittest.main()
