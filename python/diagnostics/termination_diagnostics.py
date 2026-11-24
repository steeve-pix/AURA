"""Compare raw termination modes without assigning a V2 reward or training."""
import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import aura_python
import torch
from python.aura_env import encode_observation, balance_reward
from python.aura_policy import CONTROLLED_ACTION_KEYS
from python.diagnostics.balance_diagnostics import require_legacy_equal, validate_observation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reference", type=Path, help="Pre-change legacy raw/encoded/reward snapshots")
    args = parser.parse_args()
    if args.output.exists(): raise FileExistsError("Choose a new output directory")
    args.output.mkdir(parents=True)
    reference = json.loads(args.reference.read_text()) if args.reference else None
    legacy = aura_python.AuraSimulation(termination_mode="legacy", floor_friction_mode="legacy_velocity_damping")
    v2 = aura_python.AuraSimulation(termination_mode="balance_v2", floor_friction_mode="legacy_velocity_damping")
    a = legacy.reset();
    b = v2.reset();
    reward = None;
    rows = [];
    end_a = end_b = None;
    rewards = []
    changed = {"termination_mode", "termination_version", "reward_available", "terminated", "done",
               "termination_reason", "reward"}
    for step in range(643):
        validate_observation(a);
        validate_observation(b)
        assert {k: v for k, v in a.items() if k not in changed} == {k: v for k, v in b.items() if k not in changed}
        assert torch.equal(encode_observation(a), encode_observation(b))
        if reference is not None:
            old = reference[step];
            require_legacy_equal(old["raw"], a)
            assert old["reward"] == reward
            if old["encoded"] is not None: assert old["encoded"] == encode_observation(a).tolist()
        if a["terminated"] and end_a is None: end_a = step
        if b["terminated"] and end_b is None: end_b = step
        assert b["reward"] is None and not b["reward_available"]
        rows.append({"step": step, "time": a["time"], "balance_state": a["balance_state"],
                     "legacy_standing": a["is_standing"], "legacy_terminated": a["terminated"],
                     "legacy_truncated": a["truncated"], "legacy_done": a["done"],
                     "legacy_reason": a["termination_reason"],
                     "v2_terminated": b["terminated"], "v2_truncated": b["truncated"], "v2_done": b["done"],
                     "v2_reason": b["termination_reason"], "v2_reward_available": b["reward_available"]})
        if step == 642: break
        action = {k: 0. for k in CONTROLLED_ACTION_KEYS}
        a = legacy.step_action(action);
        b = v2.step_action(action)
        # Only preserve the original production episode's rewards. Never
        # calculate legacy rewards on V2 or count post-episode reward.
        reward = balance_reward(a) if end_a is None else None
        if reward is not None: rewards.append(reward)
    with (args.output / "neutral.csv").open("x", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0]);
        writer.writeheader();
        writer.writerows(rows)
    summary = {"legacy_first_termination": end_a, "v2_first_termination": end_b,
               "compared_physics_and_neural_snapshots": len(rows), "legacy_reward_steps": len(rewards),
               "legacy_reward_sum": sum(rewards), "v2_reward": "unavailable",
               "pre_change_legacy_reference_exact_match": True if reference else None,
               "legacy_reward_sequence_sha256": hashlib.sha256(json.dumps(rewards).encode()).hexdigest(),
               "same_step_end_policy": "Both flags true; fallen reason takes precedence",
               "source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in
                                 ("include/aura/sim/Termination.hpp", "include/aura/sim/BalanceClassifier.hpp",
                                  "src/sim/BalanceClassifier.cpp",
                                  "src/sim/AuraSimulation.cpp", "src/python/AuraPython.cpp", "python/aura_env.py",
                                  "python/diagnostics/termination_diagnostics.py")}}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary))


if __name__ == "__main__": main()
