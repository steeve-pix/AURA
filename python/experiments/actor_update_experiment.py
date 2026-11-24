"""Steps 117–118: paired Adam updates, isolated from production training.

Run with PYTHONPATH=build-release:. .venv/bin/python -m
python.experiments.actor_update_experiment --output-dir models/experiments/step117-actor-update
"""
import argparse
import copy
import hashlib
import json
import torch
from pathlib import Path

from python.aura_env import LegacyAuraEnv as AuraEnv
from python.model_checkpoint import save_checkpoint, promote_best_checkpoint, load_checkpoint
from python.experiments.residual_gradient_experiment import (
    METHODS, load_reference, collect_gradient_rollouts, paired_policy_gradients,
    parameter_layout,
)
from python.train_nn import evaluate_policy

EXPLORATION_SEED = 11701
LEARNING_RATE = 0.001


def snapshot(model):
    return [p.detach().clone() for p in model.parameters()]


def maximum_change(model, before):
    return max((p.detach() - old).abs().max().item()
               for p, old in zip(model.parameters(), before))


def fresh_pair(actor):
    actors = {method: copy.deepcopy(actor).eval().requires_grad_(True) for method in METHODS}
    optimizers = {method: torch.optim.Adam(model.parameters(), lr=LEARNING_RATE)
                  for method, model in actors.items()}
    if any(optimizer.state for optimizer in optimizers.values()):
        raise RuntimeError('Adam must start with empty states')
    before = snapshot(actor)
    if any(maximum_change(model, before) for model in actors.values()):
        raise RuntimeError('Initial actor copies differ')
    return actors, optimizers


def apply_one_update(actor, optimizer, gradient):
    """Install the exact paired loss gradient, then take precisely one Adam step."""
    parameters = list(actor.parameters())
    if gradient.numel() != sum(p.numel() for p in parameters) or not torch.isfinite(gradient).all():
        raise ValueError('Finite complete actor gradient required')
    if optimizer.state:
        raise ValueError('This diagnostic requires fresh Adam states')
    offset = 0
    for parameter in parameters:
        parameter.grad = gradient[offset:offset + parameter.numel()].reshape_as(parameter).to(parameter).clone()
        offset += parameter.numel()
    optimizer.step()
    if any(state['step'].item() != 1 for state in optimizer.state.values()):
        raise RuntimeError('Expected exactly one optimizer update')
    optimizer.zero_grad(set_to_none=True)


def run_comparison(actor, critic, table, constant, output_dir, *, env_factory=AuraEnv,
                   episodes=32, exploration_seed=EXPLORATION_SEED, progress=None):
    if exploration_seed in {11501, 11502, 11601, 11602, 11603, 11604, 11605}:
        raise ValueError('Actor-update experience must be independent of earlier experiments')
    output_dir = Path(output_dir)
    actor.eval().requires_grad_(False)
    critic.eval().requires_grad_(False)
    if any(p.grad is not None for model in (actor, critic) for p in model.parameters()):
        raise ValueError('Reference networks must have no accumulated gradients')
    actor_before, critic_before = snapshot(actor), snapshot(critic)
    actors, optimizers = fresh_pair(actor)
    initial_steps = evaluate_policy(env_factory(), actor)
    if progress:
        progress(f'Initial deterministic survival: {initial_steps} steps')
    for method in METHODS:
        directory = output_dir / method
        directory.mkdir(parents=True, exist_ok=False)
        latest = directory / 'actor.pt'
        save_checkpoint(actors[method], optimizers[method], 0, latest,
                        training_seed=42, training_run_id=f'step117-{method}-42')
        promote_best_checkpoint(latest, initial_steps)
    env = env_factory()
    callback = (
        lambda i, total, steps: progress(f'Training experience {i}/{total}: {steps} steps')) if progress else None
    data = collect_gradient_rollouts(env, actor, episodes, exploration_seed, callback)
    gradients, details = paired_policy_gradients(actor, critic, data, table, constant, env.episode_limit)
    torch.save({'kind': 'paired_actor_update_rollouts', 'data': data,
                'gradients': gradients, 'parameter_layout': parameter_layout(actor)}, output_dir / 'trajectories.pt')
    rows = {}
    for method in METHODS:
        model, optimizer = actors[method], optimizers[method]
        apply_one_update(model, optimizer, gradients[method])
        steps = evaluate_policy(env_factory(), model)
        latest = output_dir / method / 'actor.pt'
        save_checkpoint(model, optimizer, episodes, latest,
                        training_seed=42, training_run_id=f'step117-{method}-42')
        promote_best_checkpoint(latest, steps)
        best = latest.with_name('actor_best.pt')
        best_saved = torch.load(best, map_location='cpu', weights_only=True)
        # Verify the actual saved policies, including the retained initial best.
        for path, expected_steps in ((latest, steps), (best, max(initial_steps, steps))):
            evaluation_env = env_factory()
            restored, _ = load_checkpoint(path, evaluation_env)
            if evaluate_policy(evaluation_env, restored) != expected_steps:
                raise RuntimeError(f'Saved checkpoint evaluation differs: {path}')
        rows[method] = {
            'initial_deterministic_survival_steps': initial_steps,
            'survival_after_update_steps': steps,
            'change_from_initial_steps': steps - initial_steps,
            'actor_gradient_norm': gradients[method].double().norm().item(),
            'maximum_parameter_change': maximum_change(model, actor_before),
            'actor_optimizer_updates': 1,
            'latest_checkpoint': str(latest), 'best_checkpoint': str(best),
            'best_survival_steps': best_saved['best_evaluation_steps'],
            'best_updates_completed': best_saved['updates_completed'],
        }
        if progress:
            progress(f'{method}: {steps} steps after one update; best {max(initial_steps, steps)}')
    actor_change, critic_change = maximum_change(actor, actor_before), maximum_change(critic, critic_before)
    if actor_change or critic_change or any(
            p.grad is not None for model in (actor, critic) for p in model.parameters()):
        raise RuntimeError('Reference actor or frozen critic changed')
    return {
        'methods': rows, 'initial_actor_copies_identical': True,
        'initial_actor_maximum_difference': 0.0, 'initial_adam_states_empty': True,
        'critic_maximum_parameter_change': critic_change, 'critic_has_no_gradients': True,
        'critic_optimizer_updates': 0, 'reference_actor_maximum_parameter_change': actor_change,
        'same_trajectories_for_both_methods': True, 'episodes': episodes,
        'exploration_seed': exploration_seed, 'actor_advantage_denominator': env.episode_limit,
        'scaled_return_to_actor_advantage_factor': 100.0 / env.episode_limit,
        'loss_reduction': 'Negative sum over timesteps / episode count; no gradient clipping',
        'baseline_fitted_to_comparison_trajectories': False,
        'critic_trained_on_comparison_trajectories': False,
        'production_trainer_changed': False, 'gradient_details': details,
        'limitations': 'One update and one actor seed. Deterministic survival is a single fixed initial-state evaluation, not evidence of robust balance or a production-method decision.',
    }


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def summarize_batches(batches):
    """Keep paired outcomes visible; five attempts are not five actor seeds."""
    import statistics
    summary = {}
    for method in METHODS:
        changes = [batch['methods'][method]['change_from_initial_steps'] for batch in batches]
        summary[method] = {
            'mean_survival_improvement_steps': statistics.mean(changes),
            'sample_std_improvement_steps': statistics.stdev(changes),
            'individual_improvements_steps': changes,
            'improved_from_initial': sum(value > 0 for value in changes),
            'unchanged_from_initial': sum(value == 0 for value in changes),
            'worse_than_initial': sum(value < 0 for value in changes),
        }
    differences = [batch['methods']['residual']['survival_after_update_steps'] -
                   batch['methods']['timestep']['survival_after_update_steps'] for batch in batches]
    return {'methods': summary, 'paired_critic_minus_timestep_steps': differences,
            'mean_paired_difference_steps': statistics.mean(differences),
            'residual_wins': sum(value > 0 for value in differences),
            'timestep_wins': sum(value < 0 for value in differences),
            'ties': sum(value == 0 for value in differences)}


def verify_reused_batch(directory, actor, critic, table, constant, protected):
    """Replay archived gradients and Adam steps before accepting Step 117 as A."""
    directory = Path(directory)
    report = json.loads((directory / 'report.json').read_text())
    if (report['exploration_seed'] != 11701 or report['episodes'] != 32 or
            report['actor_seed'] != 42 or report['action_schema'] != 5 or
            report['reference_artifacts_sha256'] != protected or
            report['optimizer']['learning_rate'] != LEARNING_RATE):
        raise ValueError('Reused batch must match the same independent Step 117 configuration')
    for name, expected in report['artifacts'].items():
        if digest(directory / name) != expected:
            raise ValueError(f'Reused batch artifact changed: {name}')
    archive = torch.load(directory / 'trajectories.pt', map_location='cpu', weights_only=True)
    if len(archive['data']['states_by_episode']) != 32:
        raise ValueError('Reused batch must contain 32 episodes')
    env = AuraEnv()
    gradients, details = paired_policy_gradients(actor, critic, archive['data'], table, constant, env.episode_limit)
    initial = evaluate_policy(env, actor)
    actors, optimizers = fresh_pair(actor)
    for method in METHODS:
        row = report['methods'][method]
        if not torch.equal(gradients[method], archive['gradients'][method]):
            raise ValueError('Reused gradient replay differs')
        apply_one_update(actors[method], optimizers[method], gradients[method])
        latest = directory / method / 'actor.pt'
        saved = torch.load(latest, map_location='cpu', weights_only=True)
        if saved['updates_completed'] != 1 or any(
                not torch.equal(value, saved['model'][name]) for name, value in actors[method].state_dict().items()):
            raise ValueError('Reused one-update checkpoint differs')
        steps = evaluate_policy(AuraEnv(), actors[method])
        if (initial != row['initial_deterministic_survival_steps'] or steps != row['survival_after_update_steps'] or
                steps - initial != row['change_from_initial_steps'] or details != report['gradient_details']):
            raise ValueError('Reused survival or loss metadata differs')
        best_env = AuraEnv()
        best, _ = load_checkpoint(directory / method / 'actor_best.pt', best_env)
        if evaluate_policy(best_env, best) != max(initial, steps):
            raise ValueError('Reused best checkpoint differs')
    return report


def run_five_batches(actor, critic, table, constant, output_dir, reused_report, progress=None):
    output_dir = Path(output_dir)
    batches = [{**reused_report, 'batch': 'A', 'reused_step117': True}]
    before_actor, before_critic = snapshot(actor), snapshot(critic)
    for label, seed in zip('BCDE', (11802, 11803, 11804, 11805)):
        directory = output_dir / f'batch-{label}'
        directory.mkdir(exist_ok=False)
        callback = (lambda line, label=label: progress(f'Batch {label}: {line}')) if progress else None
        report = run_comparison(actor, critic, table, constant, directory,
                                exploration_seed=seed, progress=callback)
        report.update(batch=label, reused_step117=False)
        (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        batches.append(report)
    if maximum_change(actor, before_actor) or maximum_change(critic, before_critic):
        raise RuntimeError('Five-batch experiment modified reference networks')
    return {'batches': batches, 'summary': summarize_batches(batches),
            'reference_actor_maximum_parameter_change': 0.0,
            'critic_maximum_parameter_change': 0.0,
            'critic_optimizer_updates': 0, 'production_trainer_changed': False,
            'limitations': 'Five independent exploration batches, one original seed-42 actor, one fixed initial state. Batch A is the previously observed result; four batches are new. No sequential learning or varied actor-seed evidence.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-dir', type=Path, default=Path('models/critics/step115-current-physics'))
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--batches', type=int, choices=(1, 5), default=1)
    parser.add_argument('--reuse-step117', type=Path, default=Path('models/experiments/step117-actor-update'))
    args = parser.parse_args()
    if args.output_dir.exists():
        parser.error('Choose an unused output directory to preserve earlier experiments')
    actor, critic, table, constant, reference = load_reference(args.reference_dir)
    protected = {str(path): digest(path) for path in args.reference_dir.iterdir() if path.is_file()}
    config = {
        'experiment': 'step117-controlled-actor-update', 'actor_seed': 42,
        'exploration_seed': EXPLORATION_SEED, 'episodes': 32,
        'observation_size': 205, 'observation_schema': reference['observation_schema'],
        'action_count': 32, 'action_schema': 5,
        'physical_action_schema': reference['physical_action_schema'],
        'discount_factor': 0.995, 'return_scale': 100.0,
        'exploration_std': actor.exploration_std, 'sample_bounds': list(actor.sample_bounds),
        'action_keys': list(actor.action_transforms),
        'optimizer': {'name': 'Adam', 'learning_rate': LEARNING_RATE, 'betas': [0.9, 0.999],
                      'eps': 1e-8, 'weight_decay': 0, 'amsgrad': False},
        'updates_per_method': 1, 'critic_updates': 0,
        'reference_artifacts_sha256': protected,
        'physics_extension_sha256': reference['physics_extension_sha256'],
        'torch_version': str(torch.__version__), 'source_sha256': digest(__file__),
    }
    reused = None
    if args.batches == 5:
        reused = verify_reused_batch(args.reuse_step117, actor, critic, table, constant, protected)
        config.update(experiment='step118-five-paired-actor-updates', independent_batches=5,
                      exploration_seeds=[11701, 11802, 11803, 11804, 11805],
                      reused_batch_a=str(args.reuse_step117),
                      reused_report_sha256=digest(args.reuse_step117 / 'report.json'))
        config.pop('exploration_seed')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    (args.output_dir / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
    (args.output_dir / 'experiment.py').write_bytes(Path(__file__).read_bytes())
    progress = lambda line: print(line, flush=True)
    if args.batches == 5:
        report = run_five_batches(actor, critic, table, constant, args.output_dir, reused, progress)
    else:
        report = run_comparison(actor, critic, table, constant, args.output_dir, progress=progress)
    if any(digest(path) != expected for path, expected in protected.items()):
        raise RuntimeError('A protected reference artifact changed')
    report.update(config)
    report['reference_artifacts_unchanged'] = True
    report['artifacts'] = {str(path.relative_to(args.output_dir)): digest(path)
                           for path in args.output_dir.rglob('*') if path.is_file() and path.suffix != '.lock'}
    (args.output_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report.get('summary', report.get('methods')), indent=2), flush=True)


if __name__ == '__main__':
    main()
