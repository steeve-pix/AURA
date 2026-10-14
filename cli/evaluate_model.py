"""Evaluate one saved policy and retain its previous comparable measurement."""

import argparse
from datetime import datetime, timezone
import hashlib
import inspect
import io
import json
import os
from pathlib import Path
import select
import sys
import tempfile
import time

import aura_python

from python.aura_env import AuraEnv, encode_observation
from python.aura_policy import outputs_to_action, CONTROLLED_ACTION_KEYS
from python.model_checkpoint import load_checkpoint
from python.train_nn import evaluate_policy
from cli.model_viewer import emit_interface, interface_state


def evaluation_protocol(env):
    """Do not compare scores measured with different physics or observation code."""
    digest = hashlib.sha256(Path(aura_python.__file__).read_bytes())
    for function in (encode_observation, outputs_to_action, evaluate_policy):
        digest.update(inspect.getsource(function).encode())
    runtime = interface_state(env)
    return {"version": 3, "implementation": digest.hexdigest(),
            "observation_size": env.observation_size, "episode_limit": env.episode_limit,
            "observation_schema": runtime["observation_schema"], "action_schema": runtime["action_schema"],
            "action_keys": list(CONTROLLED_ACTION_KEYS), "motor_states": runtime["motor_states"]}


def previous_result(path, protocol):
    try:
        data = json.loads(path.read_text())
        steps = data.get("steps")
        if data.get("protocol") == protocol and type(steps) is int and 0 <= steps <= protocol["episode_limit"]:
            return steps
    except (OSError, ValueError, TypeError, AttributeError):
        pass
    return None


def save_result(path, result):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".evaluation-", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w") as output:
            json.dump(result, output, indent=2)
            output.write("\n")
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


class EvaluationEnvironment:
    """Observe progress and cancellation without changing the evaluation function."""
    def __init__(self, env, emit, interactive):
        self.env, self.emit, self.interactive = env, emit, interactive
        self.pending = ""
        self.last_report = float("-inf")

    def service(self):
        if not self.interactive:
            return
        while select.select([sys.stdin], [], [], 0)[0]:
            try:
                data = os.read(sys.stdin.fileno(), 4096)
            except BlockingIOError:
                break
            if not data:
                raise KeyboardInterrupt
            self.pending += data.decode("ascii", errors="ignore")
        while "\n" in self.pending:
            command, self.pending = self.pending.split("\n", 1)
            if command == "quit":
                raise KeyboardInterrupt
        self.pending = self.pending[-256:]

    def reset(self):
        self.service()
        return self.env.reset()

    def step(self, action):
        self.service()
        result = self.env.step(action)
        now = time.monotonic()
        if now-self.last_report >= .1 or result[2] or result[3]:
            self.emit("progress", result[4]["steps"])
            self.last_report = now
        return result


def evaluate_checkpoint(path, emit=lambda *_: None, interactive=False):
    path = Path(path)
    env = AuraEnv()
    # Read a stable snapshot even if training replaces the latest file during evaluation.
    contents = path.read_bytes()
    policy, checkpoint = load_checkpoint(io.BytesIO(contents), env)
    updates = checkpoint.get("updates_completed", max(
        (int(state.get("step", 0)) for state in checkpoint.get("optimizer", {}).get("state", {}).values()), default=0))
    emit("start", path.name, checkpoint["episodes_completed"], updates, env.episode_limit)
    emit_interface(env, emit)
    protocol = evaluation_protocol(env)
    history = path.parent / ".evaluations" / (path.name + ".json")
    previous = previous_result(history, protocol)
    steps = evaluate_policy(EvaluationEnvironment(env, emit, interactive), policy)
    info = env.simulation.observation()
    result = {"model": path.name, "checkpoint_sha256": hashlib.sha256(contents).hexdigest(),
              "episodes_completed": checkpoint["episodes_completed"], "updates_completed": updates,
              "steps": steps, "previous_steps": previous, "termination_reason": info["termination_reason"],
              "simulation_seconds": info["time"], "evaluated_at": datetime.now(timezone.utc).isoformat(),
              "protocol": protocol}
    save_result(history, result)
    emit("result", steps, previous if previous is not None else "-", info["termination_reason"], info["time"])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("--events", action="store_true")
    parser.add_argument("--interactive", action="store_true")
    args = parser.parse_args()
    if args.interactive:
        os.set_blocking(sys.stdin.fileno(), False)
    def emit(kind, *values):
        if args.events:
            print("AURA_EVAL", kind, *values, sep="\t", flush=True)
    try:
        result = evaluate_checkpoint(args.checkpoint, emit, args.interactive)
        if not args.events:
            previous = result["previous_steps"]
            change = "first evaluation" if previous is None else f"{result['steps']-previous:+d} steps vs previous {previous}"
            print(f"{result['model']}: {result['steps']} steps / {change}")
    except KeyboardInterrupt:
        emit("stopped")
    except Exception as error:
        emit("error", str(error).replace("\n", " "))
        parser.exit(1, f"Cannot evaluate model: {error}\n")


if __name__ == "__main__":
    main()
