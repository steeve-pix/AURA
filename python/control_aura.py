"""Launch AURA and control its hip and knee targets in real time.

This builds and opens the AURA viewer, then accepts values as:
    left_hip_target right_hip_target left_knee_target right_knee_target
For example: 0.2 -0.1 1.0 1.0. Enter q to quit.
Use --prepare to hand joint editing to the AURA CLI after startup.
"""

import argparse
import json
import math
import os
import subprocess
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_DIR / "build"
ACTION_FILE = PROJECT_DIR / "viewer_action.json"


def clamp(val, min_val, max_val):
    return max(min_val, min(val, max_val))


def write_action(left_hip_target: float, right_hip_target: float, left_knee_target: float = 0.0,
                 right_knee_target: float = 0.0) -> tuple[float, float, float, float]:
    """Write a complete JSON update atomically so the viewer never reads a partial file."""
    left_hip_target = clamp(left_hip_target, -0.8, 0.8)
    right_hip_target = clamp(right_hip_target, -0.8, 0.8)
    left_knee_target = clamp(left_knee_target, 0.0, 2.2)
    right_knee_target = clamp(right_knee_target, 0.0, 2.2)

    temporary_file = ACTION_FILE.with_suffix(".json.tmp")
    contents = {
        "left_hip_target": left_hip_target,
        "right_hip_target": right_hip_target,
        "left_knee_target": left_knee_target,
        "right_knee_target": right_knee_target,
    }
    with temporary_file.open("w", encoding="utf-8") as file:
        json.dump(contents, file, indent=2)
        file.write("\n")
        file.flush()
        os.fsync(file.fileno())
    temporary_file.replace(ACTION_FILE)
    return left_hip_target, right_hip_target, left_knee_target, right_knee_target


def prepare_viewer() -> None:
    """Build and open the viewer before an interactive controller takes over."""
    print("Configuring AURA…", flush=True)
    subprocess.run(["cmake", "-S", str(PROJECT_DIR), "-B", str(BUILD_DIR)], check=True)
    print("Building AURA…", flush=True)
    subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--target", "aura"],
        check=True,
    )

    write_action(0.0, 0.0)
    print("Opening AURA with neutral joint targets…", flush=True)
    viewer = BUILD_DIR / "AURA.app"
    subprocess.run(["open", str(viewer)], check=True)
    print(f"Viewer launched. Actions: {ACTION_FILE}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--prepare", action="store_true",
        help="build and launch AURA, then return for the CLI control center",
    )
    args = parser.parse_args()
    try:
        prepare_viewer()
    except subprocess.CalledProcessError as error:
        parser.exit(1, f"AURA startup failed: {error.cmd[0]} exited with {error.returncode}.\n")
    except OSError as error:
        parser.exit(1, f"AURA startup failed: {error}\n")
    if args.prepare:
        return

    print(f"Writing actions to {ACTION_FILE}")
    print("Enter targets in this order: left hip | right hip | left knee | right knee, or q to quit.")

    while True:
        line = input("> ").strip()
        if line.lower() in {"q", "quit", "exit"}:
            write_action(0.0, 0.0)
            break
        try:
            left_hip, right_hip, left_knee, right_knee = (
                float(value) for value in line.split()
            )
        except ValueError:
            print("Enter exactly four numbers, such as: 0.2 -0.2 1.0 1.0")
            continue
        if not all(math.isfinite(value) for value in (left_hip, right_hip, left_knee, right_knee)):
            print("Please enter finite numbers.")
            continue
        left_hip, right_hip, left_knee, right_knee = (
            write_action(left_hip, right_hip, left_knee, right_knee)
        )
        print(
            f"Updated targets: left hip={left_hip}, right hip={right_hip}, "
            f"left knee={left_knee}, right knee={right_knee}"
        )


if __name__ == "__main__":
    main()
