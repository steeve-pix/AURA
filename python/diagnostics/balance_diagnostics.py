"""Record one neutral episode; no actor, critic, optimizer, or viewer is loaded."""
import argparse
import csv
import hashlib
import json
import subprocess
import sys
from math import acos, degrees, hypot, isfinite
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import aura_python
from python.aura_env import LegacyAuraEnv as AuraEnv, OBSERVATION_SCHEMA_VERSION, OBSERVATION_SIZE
from python.aura_policy import CONTROLLED_ACTION_KEYS, ACTION_SCHEMA_VERSION


def finite(value):
    if isinstance(value, dict): return all(finite(v) for v in value.values())
    if isinstance(value, (list, tuple)): return all(finite(v) for v in value)
    return isfinite(value) if isinstance(value, (float, int)) else True


def require_legacy_equal(expected, actual):
    """Compare all original fields while allowing additive raw diagnostics."""
    if isinstance(expected, dict):
        for key, value in expected.items(): require_legacy_equal(value, actual[key])
    elif isinstance(expected, list):
        if len(expected) != len(actual): raise AssertionError("Legacy sequence length changed")
        for a, b in zip(expected, actual): require_legacy_equal(a, b)
    elif expected != actual:
        raise AssertionError(f"Legacy observation changed: {expected!r} != {actual!r}")


def trace_row(obs):
    row = {"step": obs["steps"], "time": obs["time"], "is_standing": obs["is_standing"],
           "terminated": obs["terminated"], "truncated": obs["truncated"],
           "termination_reason": obs["termination_reason"], "com_height": obs["center_of_mass_height"]}
    if "balance_state" in obs:
        row["balance_state"] = obs["balance_state"]
        row["balance_state_duration"] = obs["balance_state_duration"]
        for key, value in obs["balance_conditions"].items(): row["balance_" + key] = value
        for key, value in obs["balance_timers"].items(): row["balance_timer_" + key] = value
    for name in ("pelvis", "torso", "head"):
        row[name + "_height"] = obs[name + "_height"]
        row[name + "_uprightness"] = obs[name + "_uprightness"]
        # Clamp acos input for roundoff only; never alter physical state.
        row[name + "_tilt_degrees"] = degrees(acos(max(-1., min(1., obs[name + "_uprightness"]))))
        for axis, value in zip("xyz", obs[name + "_angular_velocity"]): row[name + "_angular_velocity_" + axis] = value
    for axis, value in zip("xyz", obs["center_of_mass_velocity"]): row["com_velocity_" + axis] = value
    for axis, value in zip("xyz", obs["pelvis_velocity"]): row["pelvis_velocity_" + axis] = value
    for p in obs["body_part_states"]:
        row[p["name"] + "_geometric_contact_count"] = len(p["geometric_floor_contact_points"])
        row[p["name"] + "_floor_clearance"] = p["floor_clearance"]
    for side in ("left", "right"):
        row[side + "_foot_grounded"] = obs[side + "_foot_grounded"]
        knee = next(j for j in obs["joints"] if j["name"] == side + "Knee")
        row[side + "_knee_anchor_a_floor_clearance"] = knee["anchor_a_floor_clearance"]
        row[side + "_knee_anchor_b_floor_clearance"] = knee["anchor_b_floor_clearance"]
    row["support_vertices"] = len(obs["support_polygon"])
    row["com_inside_support_polygon"] = obs["center_of_mass_inside_support_polygon"]
    row["support_margin"] = obs["support_margin"]
    row["max_joint_anchor_gap"] = max(hypot(*(a - b for a, b in zip(j["world_anchor_a"], j["world_anchor_b"])))
                                      for j in obs["joints"])
    row["max_floor_penetration"] = max(0., max(-p["floor_clearance"] for p in obs["body_part_states"]))
    row["max_body_linear_speed"] = max(hypot(*p["velocity"]) for p in obs["body_part_states"])
    row["max_body_angular_speed"] = max(hypot(*p["angular_velocity"]) for p in obs["body_part_states"])
    row["max_orientation_norm_error"] = max(abs(hypot(*p["orientation"]) - 1.) for p in obs["body_part_states"])
    return row


def validate_observation(obs):
    if not finite(obs): raise ValueError("Nonfinite raw diagnostic measurement")
    for part in obs["body_part_states"]:
        if hypot(*part["orientation"]) == 0.:
            raise ValueError(f"Invalid zero orientation quaternion: {part['name']}")


def record_episode(env, post_termination_steps=0, max_steps=None, reference=None):
    """Continue only here, on explicit opt-in; AuraEnv itself is unchanged.

    After the first legacy episode end, call the raw simulation directly. No
    neural encoding, Python reward calculation, or training transitions are
    produced during continuation. Preserve only valid snapshots on failure.
    """
    if post_termination_steps < 0: raise ValueError("post_termination_steps must be nonnegative")
    if max_steps is None: max_steps = env.episode_limit + post_termination_steps
    if max_steps < 1: raise ValueError("max_steps must be positive")
    rows = [];
    first_contacts = {};
    reward = 0.;
    first_end = None;
    stop_reason = None;
    safety_error = None
    legacy_reentries = [];
    reference_count = 0;
    obs = None;
    contact_semantics = None
    try:
        state, obs = env.reset()
        while True:
            validate_observation(obs)
            if contact_semantics is None: contact_semantics = dict(obs["floor_contact_diagnostics"])
            index = obs["steps"]
            if reference is not None and first_end is None:
                if index >= len(reference): raise AssertionError("Episode became longer")
                require_legacy_equal(reference[index]["raw"], obs)
                if reference[index]["encoded"] != state.tolist(): raise AssertionError("205 encoded inputs changed")
                reference_count += 1
            row = trace_row(obs)
            if not finite(row): raise ValueError("Nonfinite derived diagnostic measurement")
            if rows and index != rows[-1]["step"] + 1: raise ValueError("Invalid diagnostic step progression")
            if first_end is None and (obs["terminated"] or obs["truncated"]): first_end = index
            row["phase"] = "post_termination" if first_end is not None and index > first_end else "episode"
            row["first_legacy_end_step"] = first_end if first_end is not None else ""
            rows.append(row)
            for part in obs["body_part_states"]:
                if part["geometric_floor_contact"]: first_contacts.setdefault(part["name"], index)
            if first_end is not None and index > first_end and obs["is_standing"]:
                if not rows[-2]["is_standing"]: legacy_reentries.append(index)
            if first_end is not None and index >= first_end + post_termination_steps:
                stop_reason = "post_termination_complete" if post_termination_steps else "legacy_episode_end"
                break
            if index >= max_steps:
                stop_reason = "diagnostic_step_limit";
                break
            action = {key: 0. for key in CONTROLLED_ACTION_KEYS}
            if first_end is None:
                state, r, _, _, obs = env.step(action);
                reward += r
            else:
                obs = env.simulation.step_action(action)
    except (ValueError, RuntimeError, OverflowError) as error:
        stop_reason = "numerical_safety_stop";
        safety_error = f"{type(error).__name__}: {error}"
    if reference is not None and first_end is not None and reference_count != len(reference):
        raise AssertionError("Episode became shorter")
    terminal = next((r for r in rows if r["step"] == first_end), None)
    before_end = next((r for r in rows if first_end is not None and r["step"] == first_end - 1), None)
    peaks = {name: max((r[name] for r in rows), default=None) for name in
             ("max_joint_anchor_gap", "max_floor_penetration", "max_body_linear_speed",
              "max_body_angular_speed", "max_orientation_norm_error")}
    details = {"post_termination_steps_requested": post_termination_steps, "diagnostic_step_limit": max_steps,
               "post_termination_steps_recorded": rows[-1]["step"] - first_end if rows and first_end is not None else 0,
               "stop_reason": stop_reason, "safety_error": safety_error,
               "first_legacy_end_step": first_end, "legacy_standing_reentry_steps": legacy_reentries,
               "initial": rows[0] if rows else None, "last_nonterminal": before_end, "terminal": terminal,
               "final": rows[-1] if rows else None, "first_geometric_contact_step": first_contacts,
               "mechanical_extrema": peaks, "neural_reward_sum": reward,
               "contact_semantics": contact_semantics,
               "pre_change_reference_exact_match": True if reference is not None and terminal else None}
    return rows, details


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/traces/step123-neutral")
    parser.add_argument("--reference", type=Path, help="Optional pre-change raw/encoded episode JSON")
    parser.add_argument("--post-termination-steps", type=int, default=0,
                        help="Diagnostic-only raw simulation steps after first legacy episode end (e.g. 180)")
    parser.add_argument("--max-steps", type=int, help="Absolute diagnostic simulation step cap")
    args = parser.parse_args()
    if args.post_termination_steps < 0 or (args.max_steps is not None and args.max_steps < 1):
        parser.error("Continuation must be nonnegative and max steps must be positive")
    # Never silently replace an archived calibration trace.
    args.output.mkdir(parents=True, exist_ok=True)
    csv_path = args.output / "trace.csv";
    summary_path = args.output / "summary.json"
    if csv_path.exists() or summary_path.exists(): raise FileExistsError("Choose a new trace output directory")
    reference = json.loads(args.reference.read_text()) if args.reference else None
    env = AuraEnv()
    rows, details = record_episode(env, args.post_termination_steps, args.max_steps, reference)
    with csv_path.open("x", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=rows[0].keys() if rows else ("step", "phase"))
        writer.writeheader();
        writer.writerows(rows)
    sources = ("src/sim/AuraSimulation.cpp", "include/aura/sim/AuraSimulation.hpp",
               "src/sim/BalanceClassifier.cpp", "include/aura/sim/BalanceClassifier.hpp",
               "src/python/AuraPython.cpp", "src/app/SimulationStep.cpp", "src/physics/Collision.cpp",
               "src/physics/RoomCollision.cpp", "src/physics/BodyGeometry.cpp", "src/body/AuraBody3D.cpp",
               "python/aura_env.py", "python/aura_policy.py", "python/diagnostics/balance_diagnostics.py")
    summary = {"controller": "32 zero motor targets, no exploration or network inference",
               "motor_states": env.simulation.motor_states(), "observation_size": OBSERVATION_SIZE,
               "observation_schema_version": OBSERVATION_SCHEMA_VERSION, "action_count": len(CONTROLLED_ACTION_KEYS),
               "action_schema_version": ACTION_SCHEMA_VERSION,
               "physical_action_schema_version": aura_python.action_schema_version,
               "episode_limit": env.episode_limit, "rows_including_reset": len(rows),
               "steps": rows[-1]["step"] if rows else None, "time_seconds": rows[-1]["time"] if rows else None,
               "termination_reason": details["terminal"]["termination_reason"] if details["terminal"] else None,
               "contact_limitations": "End-of-step floorShape geometry; not solver events or forces. Room uses box geometry separately. Knees have joint anchors and adjacent thigh/shin colliders, no knee collider.",
               **details,
               "pre_change_reference_sha256": hashlib.sha256(
                   args.reference.read_bytes()).hexdigest() if args.reference else None,
               "trace_sha256": hashlib.sha256(csv_path.read_bytes()).hexdigest(),
               "git_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
               "source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources}}
    summary_path.write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n")
    print(json.dumps({"trace": str(csv_path), "summary": str(summary_path), "steps": summary["steps"],
                      "termination_reason": summary["termination_reason"], "stop_reason": summary["stop_reason"],
                      "pre_change_exact_match": summary["pre_change_reference_exact_match"]}))
    if details["safety_error"]: raise SystemExit(1)


if __name__ == "__main__": main()
