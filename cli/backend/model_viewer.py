"""Watch a saved policy without exploration noise or weight updates."""

import argparse
import aura_python
import time
from math import isfinite
import torch
from pathlib import Path

from python.aura_env import AuraEnv, OBSERVATION_SCHEMA_VERSION, OBSERVATION_LAYOUT, OBSERVATION_SIZE
from python.aura_policy import outputs_to_action, ACTION_SCHEMA_VERSION, CONTROLLED_ACTION_KEYS
from python.model_checkpoint import load_checkpoint, validate_checkpoint_contract


def interface_state(env):
    """Describe actual runtime contracts and motor state for CLI presentations."""
    simulation = getattr(env, "simulation", None)
    motors = simulation.motor_states() if hasattr(simulation, "motor_states") else {}
    disabled = []
    seen = set()
    for spec in aura_python.action_specs:
        if motors.get(spec["joint_name"]) is False and spec["joint_name"] not in seen:
            disabled.append(spec["label"])
            seen.add(spec["joint_name"])
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


def checkpoint_details(checkpoint, kind, body):
    """Saved architecture and interfaces, shown only when explicitly requested."""
    yield "ROLE"
    yield "Actor / chooses motor targets" if kind == "actor" else "Critic / predicts return; controls no motors"
    yield "Body: " + body
    yield (f"World: Shared arena / {checkpoint['arena_agents']} coupled agents / collisions ON"
           if checkpoint.get('arena_agents', 1) > 1 else "World: Solo balance")
    yield "Objective: " + ("Balance System / quality reward / FALLEN termination" if checkpoint.get('reward_version') == 3
                           else "Historical reward / replay only")
    if checkpoint.get("quality_version") is not None:
        yield f"Reward quality version: {checkpoint['quality_version']}"
    material = checkpoint.get('ground_material')
    if material:
        yield (f"Ground: dry / static {material['static_friction']:.1f} / sliding {material['dynamic_friction']:.1f}"
               f" / contract {checkpoint.get('environment_version', '?')}")
    if kind == 'actor' and checkpoint.get('training_algorithm') == 'actor_critic_mc':
        yield "Learning: paired actor–critic / critic target G divided by 100"
        yield "Paired critic: 205 / 64 / 64 / 1 / Tanh hidden layers"
    yield ""
    yield "NETWORK"
    layers = checkpoint.get("layers", [])
    if layers:
        sequence = [str(layers[0].get("inputs", "?"))]
        sequence.extend(str(layer["outputs"]) if layer.get("type") == "Linear"
                        else layer.get("type", "?") for layer in layers)
        yield " -> ".join(sequence)
    else:
        weights = [value for key, value in checkpoint.get("model", {}).items()
                   if key.endswith("weight") and isinstance(value, torch.Tensor) and value.ndim == 2]
        if weights:
            yield " -> ".join(map(str, [weights[0].shape[1]] + [value.shape[0] for value in weights]))
            yield "Tanh hidden layers / linear value output"
        else:
            yield "Architecture unavailable in this checkpoint"
    yield ""
    yield "OBSERVATIONS / " + str(checkpoint.get("observation_size", "unknown")) + " inputs"
    if checkpoint.get("observation_schema_version") == OBSERVATION_SCHEMA_VERSION and checkpoint.get(
            "observation_size") == OBSERVATION_SIZE:
        for feature in OBSERVATION_LAYOUT:
            yield f"{feature['start'] + 1:3}-{feature['end']:<3}  {feature['name'].replace('_', ' ').replace('com ', 'center of mass ')} ({feature['size']})"
    else:
        yield "Archived observation layout / current layout does not apply"
    yield "Center of mass describes where AURA balances its weight."
    yield ""
    if kind == "actor":
        keys = list(checkpoint.get("action_transforms", {}))
        yield "MOVEMENTS / " + str(len(keys)) + " outputs"
        specs = {spec["key"]: spec for spec in aura_python.action_specs}
        for index, key in enumerate(keys, 1):
            spec = specs.get(key, {})
            label = spec.get("label", key.replace("_", " "))
            label = label.replace("L ", "Left ", 1) if label.startswith("L ") else label
            label = label.replace("R ", "Right ", 1) if label.startswith("R ") else label
            yield f"{index:2}  {label}"
        yield "Outputs set target angles in radians, within the actor's saved learning ranges."
        if body == "Current body":
            yield "The separate head motor is disabled; neck outputs orient the head."
    else:
        report = checkpoint.get("experiment", {})
        yield "PREDICTION / 1 output"
        scale = checkpoint.get("return_scale", report.get("return_scale", "unknown"))
        yield f"Discounted return / {scale}" if kind == "critic" else f"Residual return / {scale}"
        yield "Standalone diagnostic critic / separate from the paired training critic"
        measurements = report.get("measurements", [])
        if measurements:
            last = measurements[-1]
            yield f"Saved validation MSE: {last.get('validation_mse', 'unknown')}"
            yield f"Constant baseline MSE: {report.get('constant_validation_mse', 'unknown')}"
            yield "These measurements describe the saved experiment and its body."


def model_catalog(directory):
    """Read actor and critic metadata without modifying any checkpoint."""
    from cli.backend.saved_library import catalog_lines
    yield from catalog_lines(directory)
    env = AuraEnv()
    root = Path(directory)
    paths = list(root.glob("*.pt")) + list((root / "critics").rglob("*.pt")) + [p for p in (root/"experiments").rglob("*.pt") if p.name!="source_snapshot.pt"]
    for path in sorted(paths):
        if not path.is_file():
            continue
        inputs = outputs = 0
        checkpoint = {}
        kind, body = ("critic" if path.is_relative_to(root / "critics") else "actor"), "Unverified body"
        try:
            checkpoint = torch.load(path, map_location="cpu", weights_only=True)
            if not isinstance(checkpoint, dict):
                raise ValueError("Checkpoint must contain model metadata")
            saved_kind = checkpoint.get("kind", "")
            if saved_kind in ("aura_value", "aura_value_residual"):
                kind = "critic" if saved_kind == "aura_value" else "residual critic"
            elif path.is_relative_to(root / "critics"):
                # Trajectory archives and experiment reports are not models.
                continue
            inputs = int(checkpoint.get("observation_size", 0))
            report = checkpoint.get("experiment", {})
            policy_schema = checkpoint.get("action_schema_version", report.get("action_schema"))
            physical_schema = checkpoint.get("physical_action_schema_version", report.get("physical_action_schema"))
            if policy_schema == ACTION_SCHEMA_VERSION and (
                    kind == "actor" or physical_schema == aura_python.action_schema_version):
                body = "Current body"
            elif isinstance(policy_schema, int) and policy_schema < ACTION_SCHEMA_VERSION:
                body = "Older body"
            from python.arena_env import checkpoint_environment
            env = checkpoint_environment(checkpoint)
            if kind == "actor":
                linears = [layer for layer in checkpoint.get("layers", []) if layer.get("type") == "Linear"]
                outputs = int(linears[-1]["outputs"]) if linears else len(checkpoint.get("action_transforms", {}))
                validate_checkpoint_contract(checkpoint, env)
                from python.balance_quality import QUALITY_VERSION
                historical = checkpoint.get('quality_version') != QUALITY_VERSION or checkpoint.get('collection_mode') != 'independent' or checkpoint.get('experimental_reward') is not None or checkpoint.get('training_algorithm') != 'actor_critic_mc'
                status = "experimental" if path.is_relative_to(root/"experiments") else "historical" if historical else "compatible"
                detail = ("Experimental candidate / evaluate or Fork actor / use experiment library for continuation" if status=="experimental" else "Historical/experimental contract / inspect, watch and evaluate / Fork actor for independent default training" if historical
                          else "Resume: compatible / evaluate: compatible / workers editable / actor transfer: available")
            else:
                outputs = 1
                from python.arena_env import make_environment
                if checkpoint.get('collection_mode') == 'independent':
                    from python.independent_env import IndependentEnv
                    active = IndependentEnv(1)
                else:
                    active = make_environment(checkpoint.get('arena_agents', 1),
                                              spacing=checkpoint.get('arena_spacing', 12.))
                current_objective = all(checkpoint.get(key) == value for key, value in active.environment_contract.items())
                status = "compatible" if current_objective and body == "Current body" and checkpoint.get(
                    "observation_schema_version") == OBSERVATION_SCHEMA_VERSION and inputs == env.observation_size else "incompatible"
                detail = ("Standalone diagnostic critic / inspect only / not the paired training critic" if current_objective
                          else "Historical or incompatible objective / inspect only / not used in Balance System training")
        except (ValueError, KeyError, TypeError) as error:
            status, detail = "incompatible", str(error)
        except Exception as error:
            status, detail = "unreadable", str(error)
        name = path.relative_to(root).as_posix().encode().hex()
        detail = " ".join(detail.split())[:300]
        yield f"AURA_MODEL\t{name}\t{status}\t{inputs}\t{outputs}\t{detail}"
        yield f"AURA_MODEL_KIND\t{name}\t{kind.encode().hex()}\t{body.encode().hex()}"
        if isinstance(checkpoint, dict):
            def count(key):
                value = checkpoint.get(key)
                return value if type(value) is int and value >= 0 else -1

            yield (f"AURA_MODEL_INFO\t{name}\t{count('episodes_completed')}"
                   f"\t{count('updates_completed')}\t{count('best_evaluation_steps')}")
            best = checkpoint.get('best_balance_metrics', {})
            if isinstance(best, dict):
                steps, seconds = best.get('balanced_steps'), best.get('balanced_seconds')
                if type(steps) is int and steps >= 0 and isinstance(seconds, (int, float)) and isfinite(seconds) and seconds >= 0:
                    yield f"AURA_MODEL_BALANCE\t{name}\t{steps}\t{seconds}"
            if status != "unreadable":
                try:
                    details = list(checkpoint_details(checkpoint, kind, body))
                except (ValueError, KeyError, TypeError, AttributeError):
                    details = ["Network metadata incomplete in this checkpoint."]
                for line in details:
                    yield f"AURA_MODEL_DETAIL\t{name}\t{line.encode().hex()}"


def policy_action(policy, observation):
    with torch.inference_mode():
        output = policy(observation)
        if not torch.isfinite(output).all():
            raise ValueError("Model produced non-finite actions")
        return outputs_to_action(output.clamp(*policy.sample_bounds), policy.action_transforms)


def watch_arena(env, policy, viewer):
    from python.aura_env import encode_observation
    obs = list(env.simulation.reset())
    start = time.monotonic()
    while viewer.is_open():
        active = [i for i, info in enumerate(obs) if not info['done']]
        if active:
            actions = [policy_action(policy, encode_observation(info)) if i in active else {}
                       for i, info in enumerate(obs)]
            obs = list(env.simulation.step_actions(actions))
        viewer.set_title(f"AURA / Shared arena / {len(active)} active / collisions ON / close to return")
        viewer.render(env.simulation)
        delay = start + max(info['time'] for info in obs) - time.monotonic() if active else .02
        if delay > 0:
            time.sleep(delay)


def watch(path):
    metadata = torch.load(path, map_location='cpu', weights_only=True)
    from python.arena_env import checkpoint_environment
    env = checkpoint_environment(metadata)
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
    if getattr(env, 'is_arena', False) and not getattr(env,'is_independent',False):
        watch_arena(env, policy, viewer)
        return
    state, info = env.reset()
    start = time.monotonic()
    simulation_start = info["time"]
    finished = False
    while viewer.is_open():
        if not finished:
            state, _, terminated, truncated, info = env.step(policy_action(policy, state))
            finished = terminated or truncated
            if finished:
                print(f"{info['steps']} steps / {info['termination_reason']} (close viewer to return to models)",
                      flush=True)
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
