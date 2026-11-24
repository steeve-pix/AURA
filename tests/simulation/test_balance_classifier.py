"""Observation-only integration and archived neutral classifier replay."""
import csv
import sys
import unittest
from copy import deepcopy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import torch
from python.aura_env import LegacyAuraEnv as AuraEnv, encode_observation
from python.diagnostics.balance_diagnostics import record_episode


class BalanceClassifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env = AuraEnv()
        cls.rows, cls.info = record_episode(cls.env, 180)

    def test_neutral_state_progression_without_oscillation(self):
        changes = [r for i, r in enumerate(self.rows) if
                   not i or r["balance_state"] != self.rows[i - 1]["balance_state"]]
        self.assertEqual([r["balance_state"] for r in changes],
                         ["UNBALANCED", "BALANCED", "UNBALANCED", "FALLEN"])
        self.assertLess(changes[2]["step"], 462)
        self.assertGreaterEqual(changes[3]["step"], 514)
        self.assertEqual(self.info["first_legacy_end_step"], 462)
        self.assertEqual(self.info["terminal"]["balance_state"], "UNBALANCED")
        self.assertEqual(self.rows[-1]["step"], 642)

    def test_step124_physics_trace_is_unchanged(self):
        with (ROOT / "docs/traces/step124-neutral-collapse/trace.csv").open() as source:
            previous = list(csv.DictReader(source))
        self.assertEqual(len(previous), len(self.rows))
        for old, new in zip(previous, self.rows):
            for key, value in old.items(): self.assertEqual(value, str(new[key]), (new["step"], key))

    def test_repeated_observations_do_not_advance_timers(self):
        sim = self.env.simulation
        before = sim.observation()
        for _ in range(10):
            after = sim.observation()
            for key in ("balance_state", "balance_state_duration", "balance_conditions", "balance_timers"):
                self.assertEqual(before[key], after[key])

    def test_classifier_is_excluded_from_neural_input(self):
        env = AuraEnv();
        state, obs = env.reset()
        modified = deepcopy(obs)
        modified["balance_state"] = "FALLEN"
        modified["balance_state_duration"] = 999.
        modified["balance_conditions"] = {"unsupported_test_value": True}
        modified["balance_timers"] = {"collapse": 999.}
        self.assertTrue(torch.equal(state, encode_observation(modified)))
        self.assertEqual(state.numel(), 205)

    def test_fallen_latch_and_reset(self):
        sim = self.env.simulation
        # setUpClass reaches FALLEN; observations alone cannot unlatch it.
        self.assertEqual(sim.observation()["balance_state"], "FALLEN")
        fresh = AuraEnv()
        record_episode(fresh, 180)
        reset = fresh.simulation.reset()
        self.assertEqual(reset["balance_state"], "UNBALANCED")
        self.assertEqual(reset["balance_state_duration"], 0.)
        self.assertEqual(reset["balance_timers"], {"entering": 0., "outside": 0., "collapse": 0.})


if __name__ == "__main__": unittest.main()
