"""Latest and best evaluated checkpoints shared by training and model playback."""

from pathlib import Path
import os
import tempfile
import fcntl

import torch
from torch import nn

if __package__:
    from .aura_policy import AuraPolicy, ACTION_SCHEMA_VERSION, CONTROLLED_ACTION_KEYS
    from .aura_env import OBSERVATION_SCHEMA_VERSION
else:
    from aura_policy import AuraPolicy, ACTION_SCHEMA_VERSION, CONTROLLED_ACTION_KEYS
    from aura_env import OBSERVATION_SCHEMA_VERSION

MODEL_DIR = Path(__file__).resolve().parent.parent / "models"
DEFAULT_CHECKPOINT = MODEL_DIR / "aura_balance.pt"


def save_checkpoint(policy, optimizer, episodes_completed, path=DEFAULT_CHECKPOINT, training_seed=None, training_run_id=None):
    """Replace the latest checkpoint only after its complete contents are written."""
    updates_completed = max(
        (int(state.get("step", 0)) for state in optimizer.state.values()),
        default=0,
    )
    layers = []
    for layer in policy.network:
        if isinstance(layer, nn.Linear):
            layers.append({"type": "Linear", "inputs": layer.in_features,
                           "outputs": layer.out_features, "bias": layer.bias is not None})
        elif type(layer) in (nn.Tanh, nn.ReLU, nn.Sigmoid, nn.Identity):
            layers.append({"type": type(layer).__name__})
        else:
            raise ValueError(f"Checkpoint does not support layer {type(layer).__name__}")
    checkpoint = {
        "format_version": 1,
        "observation_schema_version": OBSERVATION_SCHEMA_VERSION,
        "action_schema_version": ACTION_SCHEMA_VERSION,
        "model": policy.state_dict(),
        "optimizer": optimizer.state_dict(),
        "observation_size": layers[0]["inputs"],
        "episodes_completed": episodes_completed,
        "layers": layers,
        "action_transforms": dict(policy.action_transforms),
        "sample_bounds": tuple(policy.sample_bounds),
        "exploration_std": policy.exploration_std,
        "updates_completed": updates_completed,
        "training_status": "trained" if updates_completed > 0 else "untrained",
        "rng_state": torch.get_rng_state(),
    }
    if training_seed is not None:
        checkpoint["training_seed"] = training_seed
    if training_run_id is not None:
        checkpoint["training_run_id"] = training_run_id
    return _write_checkpoint(checkpoint, path)


def _write_checkpoint(checkpoint, path):
    """Write completely before replacing an existing checkpoint."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as output:
            torch.save(checkpoint, output)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return path


def promote_best_checkpoint(latest_path, evaluation_steps, *, reset_best=False):
    """Copy the evaluated latest state if its score beats the persisted best.

    Call immediately after evaluating, while holding the latest model's training
    lock; do not update or replace latest between evaluation and this call.
    Returns the best path when promoted, otherwise None. Use reset_best only for
    the first evaluated batch of an explicitly restarted training run.
    A changed saved training_run_id also starts a fresh comparison; resume must
    preserve that ID so an interrupted restart cannot inherit an old best.
    """
    if type(evaluation_steps) is not int or evaluation_steps < 0:
        raise ValueError("Evaluation survival must be a nonnegative integer")
    latest_path = Path(latest_path)
    best_path = latest_path.with_name(f"{latest_path.stem}_best.pt")
    # Share the training lock convention: never replace a model being trained.
    with (best_path.parent / f".{best_path.name}.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError("Best checkpoint is in use by another session") from error
        checkpoint = torch.load(latest_path, map_location="cpu", weights_only=True)
        if checkpoint.get("format_version") != 1:
            raise ValueError("Unsupported checkpoint format")
        if best_path.exists():
            previous = torch.load(best_path, map_location="cpu", weights_only=True)
            score = previous.get("best_evaluation_steps")
            if (type(score) is not int or score < 0
                    or previous.get("best_source_model") != latest_path.name):
                raise ValueError(f"Refusing to overwrite unrelated checkpoint: {best_path.name}")
            restarted = (checkpoint.get("training_run_id") is not None
                         and checkpoint["training_run_id"] != previous.get("training_run_id"))
            if not reset_best and not restarted and evaluation_steps <= score:
                return None
        checkpoint["best_evaluation_steps"] = evaluation_steps
        checkpoint["best_source_model"] = latest_path.name
        return _write_checkpoint(checkpoint, best_path)


def validate_checkpoint_contract(checkpoint, env):
    """Check interface compatibility without constructing or changing a policy."""
    if checkpoint.get("format_version") != 1:
        raise ValueError("Unsupported checkpoint format")
    if checkpoint["observation_size"] != env.observation_size:
        raise ValueError(f"Model expects {checkpoint['observation_size']} inputs; environment has {env.observation_size}")
    if checkpoint.get("observation_schema_version") != OBSERVATION_SCHEMA_VERSION:
        saved_version = checkpoint.get("observation_schema_version", "missing")
        raise ValueError(
            f"Model observation schema {saved_version} is incompatible with "
            f"current schema {OBSERVATION_SCHEMA_VERSION}"
        )
    transforms = checkpoint["action_transforms"]
    current_keys = list(CONTROLLED_ACTION_KEYS)
    if checkpoint.get("action_schema_version") != ACTION_SCHEMA_VERSION or list(transforms) != current_keys:
        raise ValueError(
            f"Model policy action schema {checkpoint.get('action_schema_version', 'missing')} "
            f"({len(transforms)} actions) is incompatible with current policy schema "
            f"{ACTION_SCHEMA_VERSION} ({len(current_keys)} actions); create a new model"
        )
    linears = [layer for layer in checkpoint["layers"] if layer["type"] == "Linear"]
    if not linears or linears[0]["inputs"] != env.observation_size or linears[-1]["outputs"] != len(current_keys):
        raise ValueError("Saved layer dimensions do not match the current observation/action contract")


def load_checkpoint(path, env):
    """Rebuild the saved architecture, independently of today's policy defaults."""
    checkpoint = torch.load(path, map_location="cpu", weights_only=True)
    validate_checkpoint_contract(checkpoint, env)
    transforms = checkpoint["action_transforms"]
    layers = []
    inputs = env.observation_size
    activations = {cls.__name__: cls for cls in (nn.Tanh, nn.ReLU, nn.Sigmoid, nn.Identity)}
    for spec in checkpoint["layers"]:
        if spec["type"] == "Linear":
            if spec["inputs"] != inputs or spec["outputs"] < 1:
                raise ValueError("Inconsistent saved layer dimensions")
            layers.append(nn.Linear(inputs, spec["outputs"], bias=spec["bias"]))
            inputs = spec["outputs"]
        elif spec["type"] in activations:
            layers.append(activations[spec["type"]]())
        else:
            raise ValueError(f"Unsupported saved layer: {spec['type']}")
    if inputs != len(transforms):
        raise ValueError("Saved output size does not match its action transforms")
    policy = AuraPolicy(env.observation_size, hidden_sizes=(), exploration_std=checkpoint["exploration_std"])
    policy.network = nn.Sequential(*layers)
    policy.action_transforms = transforms
    policy.sample_bounds = tuple(checkpoint["sample_bounds"])
    policy.load_state_dict(checkpoint["model"], strict=True)
    if not all(torch.isfinite(value).all() for value in policy.parameters()):
        raise ValueError("Saved model contains non-finite parameters")
    return policy, checkpoint
