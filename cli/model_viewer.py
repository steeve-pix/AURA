"""Watch a saved policy without exploration noise or weight updates."""

import argparse
from pathlib import Path
import time

import torch
import aura_python

from python.aura_env import AuraEnv, OBSERVATION_SCHEMA_VERSION
from python.aura_policy import outputs_to_action, ACTION_SCHEMA_VERSION, CONTROLLED_ACTION_KEYS
from python.model_checkpoint import load_checkpoint, validate_checkpoint_contract


def interface_state(env):
    """Describe actual runtime contracts and motor state for CLI presentations."""
    simulation = getattr(env, "simulation", None)
    motors = simulation.motor_states() if hasattr(simulation, "motor_states") else {}
    disabled = [spec["label"] for spec in aura_python.action_specs
                if motors.get(spec["joint_name"]) is False]
    return {
        "inputs": env.observation_size, "actions": len(CONTROLLED_ACTION_KEYS),
        "observation_schema": OBSERVATION_SCHEMA_VERSION, "action_schema": ACTION_SCHEMA_VERSION,
        "motor_states": motors, "motor_active": sum(motors.values()), "motor_total": len(motors),
        "disabled": disabled,
    }


def emit_interface(env, emit):
    state = interface_state(env)
    emit("interface", state["inputs"], state["actions"], state["observation_schema"], state["action_schema"])
    if state["motor_total"]:
        emit("motors", state["motor_active"], state["motor_total"], ", ".join(state["disabled"]))
    return state


def model_catalog(directory):
    """Read checkpoint contracts once for the model picker; never write models."""
    env = AuraEnv()
    for path in sorted(Path(directory).glob("*.pt")):
        if not path.is_file():
            continue
        inputs = outputs = 0
        checkpoint = {}
        try:
            checkpoint = torch.load(path, map_location="cpu", weights_only=True)
            inputs = int(checkpoint.get("observation_size", 0))
            linears = [layer for layer in checkpoint.get("layers", []) if layer.get("type") == "Linear"]
            outputs = int(linears[-1]["outputs"]) if linears else len(checkpoint.get("action_transforms", {}))
            validate_checkpoint_contract(checkpoint, env)
            status, detail = "compatible", "Current observation and action contracts"
        except (ValueError, KeyError, TypeError) as error:
            status, detail = "incompatible", str(error)
        except Exception as error:
            status, detail = "unreadable", str(error)
        # Hex filenames avoid tab/newline ambiguity without shell interpretation.
        detail = " ".join(detail.split())[:300]
        yield f"AURA_MODEL\t{path.name.encode().hex()}\t{status}\t{inputs}\t{outputs}\t{detail}"
        if isinstance(checkpoint, dict):
            def count(key):
                value = checkpoint.get(key)
                return value if type(value) is int and value >= 0 else -1
            yield (f"AURA_MODEL_INFO\t{path.name.encode().hex()}\t{count('episodes_completed')}"
                   f"\t{count('updates_completed')}\t{count('best_evaluation_steps')}")


def policy_action(policy, observation):
    with torch.inference_mode():
        output = policy(observation)
        if not torch.isfinite(output).all():
            raise ValueError("Model produced non-finite actions")
        return outputs_to_action(output.clamp(*policy.sample_bounds), policy.action_transforms)


def watch(path):
    env = AuraEnv()
    policy, checkpoint = load_checkpoint(path, env)
    policy.eval()
    print(f"Loaded {path.name} / {checkpoint['episodes_completed']} training episodes", flush=True)
    runtime = interface_state(env)
    motor_text = f" / {runtime['motor_active']} active motors" if runtime["motor_total"] else ""
    disabled_text = " / " + ", ".join(runtime["disabled"]) + " off" if runtime["disabled"] else ""
    print(f"{runtime['inputs']} inputs / {runtime['actions']} outputs{motor_text}{disabled_text}", flush=True)
    print("Watching the saved policy without exploration noise. Esc closes the viewer.", flush=True)
    viewer = aura_python.AuraViewer(env.simulation)
    viewer.show()
    state, info = env.reset()
    start = time.monotonic()
    simulation_start = info["time"]
    finished = False
    while viewer.is_open():
        if not finished:
            state, _, terminated, truncated, info = env.step(policy_action(policy, state))
            finished = terminated or truncated
            if finished:
                print(f"{info['steps']} steps / {info['termination_reason']} (close viewer to return to models)", flush=True)
            viewer.set_title(f"AURA / {path.stem} / {info['steps']} steps / "
                             + (info["termination_reason"] if finished else "policy running"))
        viewer.render(env.simulation)
        # Pace from the simulator's own clock; never change its fixed timestep.
        delay = 0.02 if finished else start + info["time"] - simulation_start - time.monotonic()
        if delay > 0:
            time.sleep(delay)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", type=Path, nargs="?")
    parser.add_argument("--catalog", type=Path)
    args = parser.parse_args()
    try:
        if args.catalog is not None:
            for line in model_catalog(args.catalog):
                print(line)
        elif args.checkpoint is not None:
            watch(args.checkpoint)
        else:
            parser.error("Choose a checkpoint or --catalog directory")
    except KeyboardInterrupt:
        pass
    except Exception as error:
        parser.exit(1, f"Cannot play model: {error}\n")


if __name__ == "__main__":
    main()
