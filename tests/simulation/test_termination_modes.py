"""Experimental termination must never silently use the legacy reward."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import aura_python
import torch
from python.aura_env import AuraEnv, balance_reward, encode_observation
from python.aura_policy import CONTROLLED_ACTION_KEYS


class TerminationModeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # These endpoint checks are historical Release replays. Keep their
        # original floor response explicit now that active simulations use
        # contact-based Coulomb friction by default.
        legacy_friction = "legacy_velocity_damping"
        cls.legacy = aura_python.AuraSimulation(
            termination_mode="legacy", floor_friction_mode=legacy_friction)
        cls.v2 = aura_python.AuraSimulation(
            termination_mode="balance_v2", floor_friction_mode=legacy_friction)
        cls.pairs = [(cls.legacy.reset(), cls.v2.reset())]
        action = {k: 0. for k in CONTROLLED_ACTION_KEYS}
        for _ in range(642): cls.pairs.append((cls.legacy.step_action(action), cls.v2.step_action(action)))

    def test_primary_default_and_historical_mode_metadata(self):
        env = AuraEnv()
        self.assertEqual(env.termination_mode, "balance_v2")
        self.assertEqual(self.pairs[0][0]["termination_version"], 1)
        self.assertEqual(self.pairs[0][1]["termination_version"], 2)
        self.assertEqual(self.v2.termination_mode, "balance_v2")
        with self.assertRaises(AttributeError): self.v2.termination_mode = "legacy"
        with self.assertRaises(ValueError): aura_python.AuraSimulation(termination_mode="unknown")

    def test_neutral_termination_and_recovery_window(self):
        ends = [next(a["steps"] for a in stream if a["terminated"]) for stream in zip(*self.pairs)]
        self.assertEqual(ends, [462, 516])
        self.assertEqual(self.pairs[462][1]["balance_state"], "UNBALANCED")
        self.assertFalse(self.pairs[462][1]["done"])
        for _, v2 in self.pairs:
            self.assertEqual(v2["terminated"], v2["balance_state"] == "FALLEN")
            self.assertEqual(v2["done"], v2["terminated"] or v2["truncated"])
            self.assertEqual(v2["termination_reason"], "fallen" if v2["terminated"] else "none")

    def test_physics_classifier_and_encoded_inputs_match(self):
        changed = {"termination_mode", "termination_version", "reward_available", "terminated", "done",
                   "termination_reason", "reward"}
        for legacy, v2 in self.pairs:
            self.assertEqual({k: v for k, v in legacy.items() if k not in changed},
                             {k: v for k, v in v2.items() if k not in changed})
            self.assertTrue(torch.equal(encode_observation(legacy), encode_observation(v2)))
        self.assertEqual(len(CONTROLLED_ACTION_KEYS), 32)

    def test_v2_reward_unavailable_and_production_guard(self):
        for _, v2 in (self.pairs[0], self.pairs[462], self.pairs[516]):
            self.assertIsNone(v2["reward"])
            self.assertFalse(v2["reward_available"])
            with self.assertRaisesRegex(ValueError, "Legacy balance_reward"): balance_reward(v2)
        env = AuraEnv()
        self.assertEqual(env.reward_version, 3)
        with self.assertRaises(ValueError): AuraEnv(termination_mode="unknown")
        env.reset()
        while not env.info['done']:
            env.step({k: 0. for k in CONTROLLED_ACTION_KEYS})
        self.assertEqual(env.info['reward_total'], 0.)
        with self.assertRaisesRegex(ValueError, "Episode ended"):
            env.step({})

    def test_repeated_reads_reset_latch_and_time_cap(self):
        sim = aura_python.AuraSimulation(
            termination_mode="balance_v2", floor_friction_mode="legacy_velocity_damping")
        action = {k: 0. for k in CONTROLLED_ACTION_KEYS}
        for _ in range(516): last = sim.step_action(action)
        for _ in range(5): self.assertEqual(last, sim.observation())
        sim.step_action(action)
        self.assertTrue(sim.observation()["terminated"])
        for _ in range(1200 - 517): last = sim.step_action(action)
        self.assertTrue(last["terminated"] and last["truncated"] and last["done"])
        self.assertEqual(last["termination_reason"], "fallen")
        reset = sim.reset()
        self.assertEqual(reset["termination_mode"], "balance_v2")
        self.assertFalse(reset["terminated"] or reset["truncated"] or reset["done"])
        self.assertEqual(reset["balance_state"], "UNBALANCED")


if __name__ == "__main__": unittest.main()
