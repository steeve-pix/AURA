"""Step 111: paired raw policy gradients; never step an actor optimizer.

Reconstruct Step 110's critic deterministically, then use three new exploration
streams. Both baselines reuse each batch's exact sampled log-probability graph.
"""
import argparse
import hashlib
import json
from pathlib import Path

import torch

from .aura_env import AuraEnv
from .aura_policy import AuraPolicy
from .critic_experiment import run_critic_experiment
from .train_nn import train_batch


def run_gradient_experiment(env_factory=AuraEnv, batch_size=32,
                            exploration_seeds=(4211, 4212, 4213), progress=None):
    if len(exploration_seeds) != 3 or len(set(exploration_seeds)) != 3:
        raise ValueError("Use three independent exploration seeds")
    if set(exploration_seeds) & {4201, 4202}:
        raise ValueError("Gradient batches must be independent of critic fitting and validation")
    retained = []
    critic_report = run_critic_experiment(
        env_factory=env_factory, progress=progress,
        on_complete=lambda report, critic, optimizer: retained.append(critic))
    critic = retained[0].eval().requires_grad_(False)
    critic.zero_grad(set_to_none=True)  # Discard gradients left by its last fitting update.
    with torch.random.fork_rng(devices=[]):
        torch.manual_seed(42)
        actor = AuraPolicy(env_factory().observation_size)
    actor.eval()
    actor_before = [p.detach().clone() for p in actor.parameters()]
    critic_before = [p.detach().clone() for p in critic.parameters()]
    # Used only for train_batch's existing zero_grad interface; step is forbidden.
    class DiagnosticOptimizer:
        def zero_grad(self):
            actor.zero_grad(set_to_none=True)
        def step(self):
            raise AssertionError("Actor optimizer updates are forbidden")
    gradients = {"original": [], "critic": []}
    rows = []
    for label, seed in zip("ABC", exploration_seeds):
        if progress:
            progress("gradient " + label, 0, batch_size, 0)
        with torch.random.fork_rng(devices=[]):
            torch.manual_seed(seed)
            result = train_batch(env_factory(), actor, DiagnosticOptimizer(), batch_size,
                                 update_parameters=False, compare_critic=critic)
        comparison = result["gradient_comparison"]
        row = {"batch": label, "exploration_seed": seed,
               "timesteps": result["action_timestep_terms"],
               "survival_steps": result["episode_scores"],
               "actor_advantage_scale": comparison["actor_advantage_scale"]}
        for method in gradients:
            vector = comparison[method + "_gradient"].double()
            norm = vector.norm().item()
            if not 0 < norm < float("inf"):
                raise RuntimeError("Gradient cosine requires a finite, nonzero gradient")
            gradients[method].append(vector)
            row[method + "_gradient_norm"] = norm
            row[method + "_gradient_sha256"] = hashlib.sha256(bytes(vector.untyped_storage())).hexdigest()
        rows.append(row)
        actor.zero_grad(set_to_none=True)
    pairs = {}
    for method, vectors in gradients.items():
        pairs[method] = {name: torch.nn.functional.cosine_similarity(vectors[i], vectors[j], dim=0).item()
                         for name, i, j in (("A-B", 0, 1), ("A-C", 0, 2), ("B-C", 1, 2))}
    actor_change = max((p - old).abs().max().item() for p, old in zip(actor.parameters(), actor_before))
    critic_change = max((p - old).abs().max().item() for p, old in zip(critic.parameters(), critic_before))
    if actor_change or critic_change or any(p.grad is not None for p in critic.parameters()):
        raise RuntimeError("Gradient diagnostic modified actor or critic")
    return {"critic_reconstruction": critic_report, "batches": rows, "cosine_similarities": pairs,
            "actor_max_parameter_change": actor_change, "critic_max_parameter_change_during_comparison": critic_change,
            "actor_optimizer_updates": 0, "critic_updates_during_comparison": 0,
            "same_trajectories_for_both_baselines": True,
            "critic_advantage_formula": "(G/100 - V(s))*100/episode_limit",
            "loss_reduction": "sum over timesteps / episode count",
            "limitations": "Three batches; agreement does not establish performance improvement or low gradient variance."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Choose a new report path; existing results are preserved")
    def progress(label, episode, total, steps):
        if episode == 0 or episode == total or episode % 8 == 0:
            print(f"{label}: {episode}/{total}", flush=True)
    report = run_gradient_experiment(progress=progress)
    with args.output.open("x") as output:
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
    print(json.dumps(report["cosine_similarities"], indent=2))


if __name__ == "__main__":
    main()
