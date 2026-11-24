"""Post-gate stepping is isolated to the opt-in diagnostic recorder."""
import csv
import sys
import unittest
from copy import deepcopy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

from python.aura_env import LegacyAuraEnv as AuraEnv
from python.diagnostics.balance_diagnostics import finite, record_episode


class GuardedEnvironment(AuraEnv):
    def __init__(self):
        super().__init__();
        self.ended = False;
        self.normal_steps = 0

    def reset(self):
        self.ended = False;
        self.normal_steps = 0
        return super().reset()

    def step(self, action):
        if self.ended: raise AssertionError("Normal environment was stepped after termination")
        result = super().step(action)
        self.normal_steps += 1;
        self.ended = result[2] or result[3]
        return result


class PostTerminationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.normal, cls.normal_info = record_episode(GuardedEnvironment())
        cls.env = GuardedEnvironment()
        cls.extended, cls.extended_info = record_episode(cls.env, 180)

    def test_default_stops_at_original_episode_end(self):
        info = self.normal_info
        self.assertEqual(info["first_legacy_end_step"], 462)
        self.assertEqual(info["stop_reason"], "legacy_episode_end")
        self.assertEqual(info["post_termination_steps_recorded"], 0)
        self.assertEqual(self.normal[-1]["step"], 462)
        self.assertTrue(self.normal[-1]["terminated"])
        self.assertEqual(self.normal[-1]["termination_reason"], "fallen")

    def test_continuation_is_opt_in_and_raw_only(self):
        info = self.extended_info
        self.assertEqual(self.env.normal_steps, 462)
        self.assertEqual(info["first_legacy_end_step"], 462)
        self.assertEqual(info["post_termination_steps_recorded"], 180)
        self.assertEqual(info["stop_reason"], "post_termination_complete")
        self.assertEqual(info["final"]["step"], 642)
        self.assertEqual(self.extended[462]["phase"], "episode")
        self.assertTrue(all(r["phase"] == "post_termination" for r in self.extended[463:]))
        self.assertEqual(info["neural_reward_sum"], self.normal_info["neural_reward_sum"])
        for before, after in zip(self.normal, self.extended): self.assertEqual(before, after)

    def test_archived_step123_measurements_are_unchanged(self):
        with (ROOT / "docs/traces/step123-neutral/trace.csv").open() as source:
            archived = list(csv.DictReader(source))
        self.assertEqual(len(archived), len(self.normal))
        for expected, actual in zip(archived, self.normal):
            for key, value in expected.items(): self.assertEqual(value, str(actual[key]), (actual["step"], key))

    def test_reproducible_and_finite(self):
        repeated, info = record_episode(GuardedEnvironment(), 180)
        self.assertEqual(repeated, self.extended)
        self.assertEqual(info, self.extended_info)
        self.assertTrue(finite(repeated))
        self.assertIsNone(info["safety_error"])

    def test_step_cap_stops_continuation(self):
        rows, info = record_episode(GuardedEnvironment(), 180, max_steps=470)
        self.assertEqual(rows[-1]["step"], 470)
        self.assertEqual(info["post_termination_steps_recorded"], 8)
        self.assertEqual(info["stop_reason"], "diagnostic_step_limit")

    def test_invalid_post_gate_state_preserves_last_good_snapshot(self):
        # Reuse real diagnostic data, injecting a failure without altering C++.
        _, snapshot = AuraEnv().reset()
        snapshot["terminated"] = True;
        snapshot["is_standing"] = False
        snapshot["termination_reason"] = "fallen"
        for failure in ("nonfinite", "zero_quaternion", "exception"):
            class BrokenSimulation:
                def step_action(self, action):
                    if failure == "exception": raise RuntimeError("invalid numeric state")
                    broken = deepcopy(snapshot);
                    broken["steps"] = 1
                    if failure == "nonfinite":
                        broken["body_part_states"][0]["velocity"] = (float("nan"), 0., 0.)
                    else:
                        broken["body_part_states"][0]["orientation"] = (0., 0., 0., 0.)
                    return broken

            class Fixture:
                episode_limit = 1200
                simulation = BrokenSimulation()

                def reset(self): return None, deepcopy(snapshot)

                def step(self, action): raise AssertionError("Must use raw simulation after legacy end")

            rows, info = record_episode(Fixture(), 180)
            self.assertEqual(len(rows), 1)
            self.assertTrue(finite(rows))
            self.assertEqual(info["stop_reason"], "numerical_safety_stop")
            self.assertIsNotNone(info["safety_error"])


if __name__ == "__main__": unittest.main()
