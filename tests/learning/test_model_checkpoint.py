"""Checkpoint round trips must preserve both policy behavior and Adam history."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import torch
import aura_python
from cli.backend.model_viewer import policy_action, watch, model_catalog
from python.aura_env import AuraEnv, OBSERVATION_SCHEMA_VERSION
from python.aura_policy import AuraPolicy, ACTION_SCHEMA_VERSION
from python.model_checkpoint import load_checkpoint, save_checkpoint as historical_save_checkpoint


def save_checkpoint(policy, optimizer, episodes, path):
    """Current-contract fixtures carry their paired value model."""
    from python.train_nn import create_critic, TrainingConfig
    env = AuraEnv()
    policy.environment_contract = env.environment_contract
    critic, critic_optimizer = create_critic(env, TrainingConfig())
    return historical_save_checkpoint(policy, optimizer, episodes, path,
                                      critic=critic, critic_optimizer=critic_optimizer)



class CheckpointTests(unittest.TestCase):
    def test_saved_policy_playback_uses_live_simulation_without_mutating_checkpoint(self):
        env = AuraEnv()
        policy = AuraPolicy(env.observation_size)
        optimizer = torch.optim.Adam(policy.parameters())

        class HeadlessViewer:
            def __init__(self, simulation):
                self.simulation = simulation

            def show(self):
                pass

            def is_open(self):
                observation = self.simulation.observation()
                return not (observation["terminated"] or observation["truncated"])

            def set_title(self, title):
                pass

            def render(self, simulation):
                if simulation is not self.simulation:
                    raise AssertionError("Viewer must use the policy's simulation")

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "balance.pt"
            save_checkpoint(policy, optimizer, 0, path)
            original = path.read_bytes()
            with patch("cli.backend.model_viewer.aura_python.AuraViewer", HeadlessViewer), patch("cli.backend.model_viewer.time.sleep"):
                watch(path)
            self.assertEqual(path.read_bytes(), original)

    def test_catalog_checks_contracts_without_mutating_models(self):
        env = AuraEnv()
        policy = AuraPolicy(env.observation_size)
        optimizer = torch.optim.Adam(policy.parameters())
        with tempfile.TemporaryDirectory() as directory:
            good = Path(directory) / "current.pt"
            old = Path(directory) / "old.pt"
            bad = Path(directory) / "broken.pt"
            save_checkpoint(policy, optimizer, 0, good)
            checkpoint = torch.load(good, weights_only=True)
            checkpoint['best_balance_metrics'] = dict(balanced_steps=72, balanced_seconds=.6)
            torch.save(checkpoint, good)
            checkpoint["action_schema_version"] = 1
            torch.save(checkpoint, old)
            bad.write_bytes(b"invalid checkpoint")
            original = {p: p.read_bytes() for p in (good, old, bad)}
            catalog = list(model_catalog(directory))
            self.assertIn(f"AURA_MODEL_BALANCE\t{good.name.encode().hex()}\t72\t0.6", catalog)
            details = [bytes.fromhex(parts[2]).decode() for parts in (row.split("\t") for row in catalog)
                       if parts[0] == "AURA_MODEL_DETAIL" and parts[1] == good.name.encode().hex()]
            self.assertIn("MOVEMENTS / 32 outputs", details)
            self.assertIn("19  Left hip turn", details)
            self.assertIn("32  Right ankle tilt", details)
            rows = {bytes.fromhex(parts[1]).decode(): parts
                    for parts in (row.split("\t") for row in catalog)
                    if parts[0] == "AURA_MODEL"}
            self.assertEqual(rows[good.name][2:5], ["historical", "205", "32"])
            self.assertEqual(rows[old.name][2], "incompatible")
            self.assertEqual(rows[bad.name][2], "unreadable")
            self.assertEqual(original, {p: p.read_bytes() for p in original})

    def test_library_separates_critics_archives_and_body_provenance(self):
        from python.aura_policy import AuraValue
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            nested = root / "critics" / "run"
            nested.mkdir(parents=True)
            critic = {"kind": "aura_value", "model": AuraValue(205).state_dict(),
                      "observation_size": 205, "observation_schema_version": OBSERVATION_SCHEMA_VERSION,
                      "action_schema_version": ACTION_SCHEMA_VERSION,
                      "physical_action_schema_version": aura_python.action_schema_version,
                      "return_scale": 100.0, **AuraEnv().environment_contract}
            torch.save(critic, nested / "critic.pt")
            historical = {**critic, 'reward_version': 1}
            torch.save(historical, nested / "historical.pt")
            torch.save({"kind": "trajectories", "states": []}, nested / "validation.pt")
            critic["action_schema_version"] = ACTION_SCHEMA_VERSION - 1
            torch.save(critic, nested / "older.pt")
            original = {p: p.read_bytes() for p in nested.glob("*.pt")}
            rows = [row.split("\t") for row in model_catalog(root)]
            models = {bytes.fromhex(row[1]).decode(): row for row in rows if row[0] == "AURA_MODEL"}
            self.assertEqual(set(models), {"critics/run/critic.pt", "critics/run/older.pt", "critics/run/historical.pt"})
            self.assertEqual(models["critics/run/critic.pt"][2:5], ["compatible", "205", "1"])
            self.assertEqual(models["critics/run/older.pt"][2], "incompatible")
            self.assertEqual(models["critics/run/historical.pt"][2], "incompatible")
            self.assertIn("Historical or incompatible objective", models["critics/run/historical.pt"][5])
            roles = {bytes.fromhex(row[1]).decode(): [bytes.fromhex(x).decode() for x in row[2:]]
                     for row in rows if row[0] == "AURA_MODEL_KIND"}
            self.assertEqual(roles["critics/run/critic.pt"], ["critic", "Current body"])
            self.assertEqual(roles["critics/run/older.pt"], ["critic", "Older body"])
            details = [bytes.fromhex(row[2]).decode() for row in rows
                       if row[0] == "AURA_MODEL_DETAIL" and row[1] == "critics/run/critic.pt".encode().hex()]
            self.assertIn("205 -> 64 -> 64 -> 1", details)
            self.assertTrue(any("joint relative orientations (60)" in line for line in details))
            self.assertEqual(original, {p: p.read_bytes() for p in original})

    def test_round_trip_and_continued_adam_update(self):
        env = AuraEnv()
        policy = AuraPolicy(env.observation_size, hidden_sizes=(13, 7, 5), exploration_std=.4)
        policy.network[1] = torch.nn.ReLU()
        policy.sample_bounds = (-.8, .9)
        optimizer = torch.optim.Adam(policy.parameters(), lr=.002)
        state, _ = env.reset()

        def update(model, adam):
            adam.zero_grad()
            model(state).square().sum().backward()
            adam.step()

        update(policy, optimizer)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested" / "balance.pt"
            save_checkpoint(policy, optimizer, 5, path)
            loaded, data = load_checkpoint(path, env)
            self.assertEqual(data["episodes_completed"], 5)
            self.assertEqual(data["observation_size"], env.observation_size)
            self.assertEqual(loaded.sample_bounds, policy.sample_bounds)
            self.assertEqual(loaded.exploration_std, .4)
            self.assertEqual(policy_action(loaded, state), policy_action(policy, state))
            resumed = torch.optim.Adam(loaded.parameters())
            resumed.load_state_dict(data["optimizer"])
            update(policy, optimizer)
            update(loaded, resumed)
            for actual, expected in zip(loaded.parameters(), policy.parameters()):
                self.assertTrue(torch.equal(actual, expected))
            save_checkpoint(loaded, resumed, 10, path)
            self.assertEqual(load_checkpoint(path, env)[1]["episodes_completed"], 10)
            original = path.read_bytes()
            with patch("python.model_checkpoint.torch.save", side_effect=OSError("disk full")):
                with self.assertRaises(OSError):
                    save_checkpoint(loaded, resumed, 15, path)
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(list(path.parent.iterdir()), [path])

    def test_old_action_schema_and_reordered_actions_are_rejected_without_changes(self):
        env = AuraEnv()
        policy = AuraPolicy(env.observation_size)
        optimizer = torch.optim.Adam(policy.parameters())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "historical.pt"
            save_checkpoint(policy, optimizer, 0, path)
            original = torch.load(path, weights_only=True)
            self.assertEqual(original["action_schema_version"], ACTION_SCHEMA_VERSION)
            for version in (None, ACTION_SCHEMA_VERSION - 1):
                checkpoint = dict(original)
                checkpoint["action_transforms"] = dict(list(original["action_transforms"].items())[:6])
                if version is None:
                    checkpoint.pop("action_schema_version")
                else:
                    checkpoint["action_schema_version"] = version
                torch.save(checkpoint, path)
                before = path.read_bytes()
                with self.assertRaisesRegex(ValueError, "6 actions.*32 actions"):
                    load_checkpoint(path, env)
                self.assertEqual(path.read_bytes(), before)

            # Matching tensor counts alone are insufficient: old body versions
            # must not resume silently against a new physical control contract.
            old_body = dict(original)
            old_body["action_schema_version"] = 3
            torch.save(old_body, path)
            before = path.read_bytes()
            with self.assertRaisesRegex(ValueError, "schema 3.*schema 5"):
                load_checkpoint(path, env)
            self.assertEqual(path.read_bytes(), before)

            historical_15 = dict(original)
            historical_transforms = {}
            for key, value in original["action_transforms"].items():
                if key == "left_shoulder_target":
                    historical_transforms["head_target"] = (0.1, False)
                historical_transforms[key] = value
            historical_transforms = dict(list(historical_transforms.items())[:15])
            historical_15["action_transforms"] = historical_transforms
            historical_15["action_schema_version"] = aura_python.action_schema_version
            historical_15["layers"] = [dict(layer) for layer in original["layers"]]
            historical_15["layers"][-2]["outputs"] = 15
            torch.save(historical_15, path)
            before = path.read_bytes()
            with self.assertRaisesRegex(ValueError, "15 actions.*32 actions"):
                load_checkpoint(path, env)
            self.assertEqual(path.read_bytes(), before)

            original["action_transforms"] = dict(reversed(list(original["action_transforms"].items())))
            torch.save(original, path)
            with self.assertRaisesRegex(ValueError, "action schema"):
                load_checkpoint(path, env)

    def test_incompatible_and_nonfinite_models_are_rejected(self):
        env = AuraEnv()
        policy = AuraPolicy(env.observation_size)
        optimizer = torch.optim.Adam(policy.parameters())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "balance.pt"
            save_checkpoint(policy, optimizer, 0, path)
            data = torch.load(path, weights_only=True)
            self.assertEqual(data["observation_schema_version"], OBSERVATION_SCHEMA_VERSION)
            legacy = dict(data)
            legacy.pop("observation_schema_version")
            torch.save(legacy, path)
            with self.assertRaisesRegex(ValueError, "observation schema"):
                load_checkpoint(path, env)
            torch.save(data, path)
            data["observation_size"] += 1
            torch.save(data, path)
            with self.assertRaisesRegex(ValueError, "inputs"):
                load_checkpoint(path, env)
            with torch.no_grad():
                next(policy.parameters()).flatten()[0] = float("nan")
            save_checkpoint(policy, optimizer, 0, path)
            with self.assertRaisesRegex(ValueError, "non-finite"):
                load_checkpoint(path, env)


if __name__ == "__main__":
    unittest.main()
