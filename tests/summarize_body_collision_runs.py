#!/usr/bin/env python3
"""Summarize staged 10-second collision runs; fail on residual overlap >1 mm.

Usage: python3 tests/summarize_body_collision_runs.py build/body_collision_batch*_10s.csv
The diagnostic itself exits on nonfinite state only; this separate gate also
checks collision quality, attachments and room containment without retuning them.
"""
import json
import re
import sys
from pathlib import Path


def fields(line):
    return dict(re.findall(r"(\w+)=([^,\s]+)", line))


def summarize(path):
    lines = Path(path).read_text().splitlines()
    def row(prefix):
        return fields(next(line for line in lines if line.startswith(prefix)))
    state = row("finite=")
    run = row("Run summary:")
    velocity = row("Velocity experiment:")
    contact = row("bodySelfCollisionSummary,")
    room = row("roomSummary,")
    hinge_line = next((line for line in lines if line.startswith("hingeAxisSummary,")), None)
    hinge = fields(hinge_line) if hinge_line else None
    environment_line = next((line for line in lines if line.startswith("bodyCollisionEnvironmentProjection,")), None)
    result = {
        "report": str(path), "batch": int(contact["batch"]),
        "pairs": int(contact["pairs"]), "finite": state["finite"] == "yes",
        "max_penetration_m": float(contact["maxPenetration"]),
        "worst_pair": contact["worstPair"], "worst_time_s": float(contact["worstTime"]),
        "max_joint_gap_m": float(run["maxAnchorGap"]),
        "minimum_y_m": float(state["minimumLowestY"]),
        "max_room_penetration_m": float(room["maximumCompletedStepBoxPenetration"]),
        "max_position_distance_m": float(state["maxPositionDistance"]),
        "peak_ke_j": float(velocity["peakKinetic"]),
        "peak_angular_speed_rad_s": float(velocity["peakAngularSpeed"]),
        "largest_contact_impulse_ns": float(contact["largestImpulse"]),
        "max_positive_contact_ke_j": float(contact["maxPositiveImpulseKE"]),
        "max_contact_momentum_error_kg_m_s": float(contact["maxImpulseMomentumError"]),
        "overlap_steps": int(contact["overlapSteps"]),
        "accepted_projections": int(contact["acceptedProjections"]),
        "blocked_projections": int(contact["blockedProjections"]),
    }
    result["passes_collision_baseline"] = (
        result["finite"] and result["max_penetration_m"] <= 0.001
        and result["max_joint_gap_m"] <= 0.001
        and result["minimum_y_m"] >= -1e-6
        and result["max_room_penetration_m"] <= 1e-6
        and run["firstFailingJoint"] == "none"
        and int(room["groupFitFailures"]) == 0
    )
    if hinge:
        result["max_hinge_swing_rad"] = float(hinge["maximumCompletedStepSwing"])
        result["max_positive_hinge_impulse_ke_j"] = float(hinge["maxPositiveDeltaKE"])
        result["passes_collision_baseline"] &= result["max_hinge_swing_rad"] <= 1e-5
    if environment_line:
        environment = fields(environment_line)
        result["accepted_environment_projections"] = int(environment["count"])
        result["max_environment_translation_m"] = float(environment["maximumTranslation"])
    return result


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    results = [summarize(path) for path in sys.argv[1:]]
    print(json.dumps(results, indent=2))
    raise SystemExit(0 if all(r["passes_collision_baseline"] for r in results) else 1)
