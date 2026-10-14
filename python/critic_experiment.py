"""Step 110: critic-only learning on whole, independently sampled episodes.

Run with PYTHONPATH=build-release:. python3 -m python.critic_experiment.
This experiment never constructs an actor optimizer or saves a model checkpoint.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path

import torch
import aura_python

from .aura_env import AuraEnv, OBSERVATION_SCHEMA_VERSION
from .aura_policy import AuraPolicy, AuraValue, ACTION_SCHEMA_VERSION
from .model_checkpoint import load_checkpoint
from .train_nn import (
    CRITIC_RETURN_SCALE, DISCOUNT_FACTOR, collect_critic_rollouts,
    prepare_critic_data, train_critic,
)


def run_critic_experiment(training_episodes=32, validation_episodes=16, updates=20,
                          actor_seed=42, critic_seed=43, training_seed=4201,
                          validation_seed=4202, learning_rate=0.001,
                          env_factory=AuraEnv, progress=None, measurement=None, service=None,
                          actor_checkpoint=None, on_complete=None):
    if training_seed == validation_seed:
        raise ValueError("Training and validation require independent exploration seeds")
    if type(updates) is not int or updates < 1:
        raise ValueError("updates must be a positive integer")
    if not 0 < learning_rate < float("inf"):
        raise ValueError("learning_rate must be positive and finite")
    if service is not None:
        service()
    training_env, validation_env = env_factory(), env_factory()
    actor_contents = Path(actor_checkpoint).read_bytes() if actor_checkpoint else None
    with torch.random.fork_rng(devices=[]):
        torch.manual_seed(actor_seed)
        actor = (load_checkpoint(io.BytesIO(actor_contents), training_env)[0] if actor_contents is not None
                 else AuraPolicy(training_env.observation_size))
        torch.manual_seed(critic_seed)
        critic = AuraValue(training_env.observation_size)
    actor.requires_grad_(False)
    actor.eval()
    actor_before = [parameter.detach().clone() for parameter in actor.parameters()]
    critic_before = [parameter.detach().clone() for parameter in critic.parameters()]
    datasets = []
    for label, env, episodes, seed in (
        ("training", training_env, training_episodes, training_seed),
        ("validation", validation_env, validation_episodes, validation_seed),
    ):
        callback = (lambda episode, total, steps, label=label: progress(label, episode, total, steps)) if progress else None
        datasets.append(collect_critic_rollouts(env, actor, episodes, seed, callback, service=service))
    training, validation = datasets
    training_states, training_targets = prepare_critic_data(
        critic, training["states_by_episode"], training["returns_by_episode"])
    validation_states, validation_targets = prepare_critic_data(
        critic, validation["states_by_episode"], validation["returns_by_episode"])
    # Fit the constant to training timesteps only, never to validation targets.
    constant = training_targets.mean()
    baseline_mse = torch.nn.functional.mse_loss(
        constant.expand_as(validation_targets), validation_targets).item()
    optimizer = torch.optim.Adam(critic.parameters(), lr=learning_rate)
    measurements = []

    def measure(update):
        was_training = critic.training
        critic.eval()
        try:
            with torch.no_grad():
                training_mse = torch.nn.functional.mse_loss(critic(training_states), training_targets).item()
                validation_mse = torch.nn.functional.mse_loss(critic(validation_states), validation_targets).item()
        finally:
            critic.train(was_training)
        row = {"update": update, "training_mse": training_mse, "validation_mse": validation_mse}
        measurements.append(row)
        if measurement is not None:
            measurement(row, baseline_mse, constant.item(), len(training_targets), len(validation_targets))

    measure(0)
    for update in range(1, updates+1):
        if service is not None:
            service()
        train_critic(critic, optimizer, training["states_by_episode"], training["returns_by_episode"])
        if update in (1, 5, 10, 20) or update == updates:
            measure(update)  # Report after the update, not train_critic's pre-update loss.
    actor_change = max((parameter.detach()-before).abs().max().item()
                       for parameter, before in zip(actor.parameters(), actor_before))
    no_actor_gradients = all(parameter.grad is None for parameter in actor.parameters())
    if actor_change != 0.0 or not no_actor_gradients:
        raise RuntimeError("Frozen actor was modified or accumulated gradients")
    report = {
        "actor_seed": actor_seed if actor_checkpoint is None else None, "critic_seed": critic_seed,
        "actor_source": str(actor_checkpoint) if actor_checkpoint else "fresh seed-"+str(actor_seed),
        "actor_checkpoint_sha256": hashlib.sha256(actor_contents).hexdigest() if actor_contents is not None else None,
        "training_exploration_seed": training_seed, "validation_exploration_seed": validation_seed,
        "training_episodes": training_episodes, "validation_episodes": validation_episodes,
        "training_timesteps": len(training_targets), "validation_timesteps": len(validation_targets),
        "training_survival_steps": training["episode_scores"],
        "validation_survival_steps": validation["episode_scores"],
        "training_raw_return_range": [training_targets.min().item()*CRITIC_RETURN_SCALE,
                                      training_targets.max().item()*CRITIC_RETURN_SCALE],
        "validation_raw_return_range": [validation_targets.min().item()*CRITIC_RETURN_SCALE,
                                        validation_targets.max().item()*CRITIC_RETURN_SCALE],
        "discount_factor": DISCOUNT_FACTOR, "return_scale": CRITIC_RETURN_SCALE,
        "optimizer": "Adam", "learning_rate": learning_rate, "critic_updates": updates,
        "observation_size": training_env.observation_size, "action_count": len(actor.action_transforms),
        "observation_schema": OBSERVATION_SCHEMA_VERSION, "action_schema": ACTION_SCHEMA_VERSION,
        "device": str(next(actor.parameters()).device), "torch_version": str(torch.__version__),
        "physics_extension_sha256": hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest(),
        "constant_scaled_prediction": constant.item(), "constant_validation_mse": baseline_mse,
        "measurements": measurements, "actor_optimizer_updates": 0,
        "actor_max_parameter_change": actor_change, "actor_has_no_gradients": no_actor_gradients,
        "critic_max_parameter_change": max((parameter.detach()-before).abs().max().item()
            for parameter, before in zip(critic.parameters(), critic_before)),
        "mse_units": "squared discounted-return units after division by 100; weighted by timestep",
        "validation_used_for_optimization": False,
    }
    if on_complete is not None:
        on_complete(report, critic, optimizer)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Save a new JSON report; never replace an existing file")
    args = parser.parse_args()
    if args.output and args.output.exists():
        parser.error("Output already exists; choose a new report name")

    def progress(label, episode, total, steps):
        if episode == 1 or episode % 8 == 0 or episode == total:
            print(f"Collected {label} episode {episode}/{total}: {steps} steps", flush=True)

    result = run_critic_experiment(progress=progress)
    print("\nUpdate | Training MSE | Validation MSE")
    for measurement in result["measurements"]:
        print(f"{measurement['update']:6d} | {measurement['training_mse']:.8f} | {measurement['validation_mse']:.8f}")
    print(f"Constant validation MSE: {result['constant_validation_mse']:.8f}")
    print(f"Actor maximum parameter change: {result['actor_max_parameter_change']:.8f}")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x") as output:
            json.dump(result, output, indent=2, allow_nan=False)
            output.write("\n")
        print(f"Report: {args.output}")


if __name__ == "__main__":
    main()
