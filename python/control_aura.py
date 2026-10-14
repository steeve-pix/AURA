"""Launch AURA and control its joint targets in real time.

This builds and opens the AURA viewer, then accepts targets in action_specs order.
For example: 0.2 -0.1 1.0 1.0 controls the original hips/knees; omitted targets
remain neutral. Up to 15 joint targets are supported. Enter q to quit.
Use --prepare to hand joint editing to the AURA CLI after startup.
"""

import argparse
import json
import math
import os
import sys
import subprocess
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_DIR / "build-release"
ACTION_FILE = PROJECT_DIR / "viewer_action.json"


def clamp(val, min_val, max_val):
    return max(min_val, min(val, max_val))


def action_specs():
    # Load only after preparation has built the Python extension.
    if str(BUILD_DIR) not in sys.path:
        sys.path.insert(0, str(BUILD_DIR))
    import aura_python
    return aura_python.action_specs


def write_action(*targets: float) -> tuple[float, ...]:
    """Atomically write targets in simulator order; omitted targets are neutral."""
    specs = action_specs()
    if len(targets) > len(specs):
        raise ValueError("Too many targets for the simulator's action contract")
    requested = (*targets, *([0.0] * (len(specs) - len(targets))))
    if not all(math.isfinite(value) for value in requested):
        raise ValueError("Joint targets must be finite")
    values = [clamp(value, spec["minimum"], spec["maximum"])
              for value, spec in zip(requested, specs)]

    temporary_file = ACTION_FILE.with_suffix(".json.tmp")
    contents = {spec["key"]: value for spec, value in zip(specs, values)}
    with temporary_file.open("w", encoding="utf-8") as file:
        json.dump(contents, file, indent=2)
        file.write("\n")
        file.flush()
        os.fsync(file.fileno())
    temporary_file.replace(ACTION_FILE)
    return tuple(values)


def prepare_viewer() -> None:
    """Build and open the viewer before an interactive controller takes over."""
    print("Configuring AURA…", flush=True)
    subprocess.run(["cmake", "-S", str(PROJECT_DIR), "-B", str(BUILD_DIR),
                    "-DCMAKE_BUILD_TYPE=Release"], check=True)
    print("Building AURA…", flush=True)
    subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--target", "aura", "aura_python"],
        check=True,
    )

    write_action()
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
    specs = action_specs()
    print("Enter targets in this order: " + " | ".join(spec["label"] for spec in specs) + ", or q to quit.")

    while True:
        line = input("> ").strip()
        if line.lower() in {"q", "quit", "exit"}:
            write_action()
            break
        try:
            targets = [float(value) for value in line.split()]
            if len(targets) != len(specs):
                raise ValueError(f"Enter exactly {len(specs)} numbers")
            values = write_action(*targets)
        except ValueError as error:
            print(error)
            continue
        print("Updated targets: " + ", ".join(f"{spec['label']}={value}" for spec, value in zip(specs, values)))


if __name__ == "__main__":
    main()
