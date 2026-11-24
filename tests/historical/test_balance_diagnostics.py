"""Raw diagnostic fields must not change the policy observation contract."""
import sys
import unittest
from copy import deepcopy
from math import isfinite
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import torch
from python.aura_env import AuraEnv, encode_observation, OBSERVATION_SIZE, OBSERVATION_SCHEMA_VERSION
from python.aura_policy import CONTROLLED_ACTION_KEYS, ACTION_SCHEMA_VERSION


class BalanceDiagnosticsTests(unittest.TestCase):
    def test_raw_posture_and_contact_fields(self):
        state, obs = AuraEnv().reset()
        by_name = {part["name"]: part for part in obs["body_part_states"]}
        self.assertEqual(len(by_name), 16)
        for name, height in (("pelvis", 4.77), ("torso", 6.6), ("head", 7.84)):
            self.assertAlmostEqual(obs[name + "_height"], height, places=5)
            self.assertEqual(obs[name + "_uprightness"], 1.)
            self.assertEqual(obs[name + "_angular_velocity"], (0., 0., 0.))
            self.assertEqual(obs[name + "_height"], by_name[name]["position"][1])
        self.assertEqual(obs["center_of_mass_height"], obs["center_of_mass_position"][1])
        self.assertEqual(obs["floor_contact_diagnostics"]["kind"], "end_of_step_geometric_proximity")
        for p in by_name.values():
            self.assertFalse(p["geometric_floor_contact"])
            self.assertEqual(p["geometric_floor_contact_points"], [])
            self.assertTrue(isfinite(p["floor_clearance"]))
        for knee in ("leftKnee", "rightKnee"):
            joint = next(j for j in obs["joints"] if j["name"] == knee)
            self.assertEqual(joint["anchor_a_floor_clearance"], joint["world_anchor_a"][1])
            self.assertEqual(joint["anchor_b_floor_clearance"], joint["world_anchor_b"][1])
        self.assertTrue(torch.isfinite(state).all())

    def test_diagnostics_are_excluded_from_205_inputs(self):
        state, obs = AuraEnv().reset()
        legacy = deepcopy(obs)
        for key in ("torso_height", "torso_uprightness", "torso_angular_velocity",
                    "head_height", "head_uprightness", "head_angular_velocity",
                    "center_of_mass_height", "floor_contact_diagnostics"):
            legacy.pop(key)
        for p in legacy["body_part_states"]:
            for key in tuple(p):
                if key not in ("name", "velocity"): p.pop(key)
        for j in legacy["joints"]:
            for key in tuple(j):
                if key not in ("name", "relative_orientation", "relative_angular_velocity"): j.pop(key)
        self.assertTrue(torch.equal(state, encode_observation(legacy)))
        self.assertEqual(OBSERVATION_SIZE, 205)
        self.assertEqual(OBSERVATION_SCHEMA_VERSION, 2)
        self.assertEqual(len(CONTROLLED_ACTION_KEYS), 32)
        self.assertEqual(ACTION_SCHEMA_VERSION, 5)

    def test_finite_reporting_after_landing(self):
        env = AuraEnv();
        env.reset()
        landed = False
        for _ in range(120):
            state, _, terminated, truncated, obs = env.step({key: 0. for key in CONTROLLED_ACTION_KEYS})
            self.assertTrue(torch.isfinite(state).all())
            for p in obs["body_part_states"]:
                self.assertTrue(
                    all(isfinite(v) for key in ("position", "orientation", "up", "angular_velocity") for v in p[key]))
                self.assertTrue(isfinite(p["floor_clearance"]))
                self.assertEqual(p["geometric_floor_contact"], bool(p["geometric_floor_contact_points"]))
            for side in ("left", "right"):
                p = next(p for p in obs["body_part_states"] if p["name"] == side + "_foot")
                self.assertEqual(p["geometric_floor_contact_points"], obs[side + "_foot_contact_points"])
                landed |= obs[side + "_foot_grounded"]
            if terminated or truncated: break
        self.assertTrue(landed)


if __name__ == "__main__": unittest.main()
