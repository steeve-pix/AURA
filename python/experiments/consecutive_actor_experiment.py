"""Step 121: isolated three-update, three-initialization on-policy pilot.

Each method fits its baseline on 32 separate on-policy episodes, then collects
32 fresh gradient episodes. Only B fits a residual critic (20 Adam steps per
round, warm-started weights and optimizer). Actor Adam states persist for three
updates. Evaluation is held out, with 64 matched episode seeds per actor seed.
"""
import argparse
import aura_python
import copy
import json
import math
import multiprocessing
import statistics
import torch
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from python.experiments.actor_update_experiment import digest, snapshot, maximum_change
from python.aura_env import LegacyAuraEnv as AuraEnv, OBSERVATION_SCHEMA_VERSION
from python.aura_policy import AuraPolicy, AuraValue, ACTION_SCHEMA_VERSION
from python.model_checkpoint import save_checkpoint, _write_checkpoint
from python.experiments.residual_critic_experiment import residual_dataset
from python.experiments.residual_gradient_experiment import load_reference, collect_gradient_rollouts, parameter_layout
from python.experiments.stochastic_policy_experiment import evaluate_stochastic
from python.experiments.timestep_baseline_analysis import fit_timestep_baseline
from python.train_nn import prepare_critic_data, evaluate_policy, CRITIC_RETURN_SCALE, DISCOUNT_FACTOR

METHODS = ('timestep', 'residual')
ACTOR_SEEDS = (42, 43, 44)
UPDATES = 3
FIT_EPISODES = 32
GRADIENT_EPISODES = 32
CRITIC_STEPS = 20
RATE = .001


def streams(actor_seed, update):
    base = 121000000 + actor_seed * 1000 + update * 10
    return {'fit': base, 'gradient': base + 1}


def evaluation_seeds(actor_seed):
    return tuple(122000000 + actor_seed * 1000 + i for i in range(64))


def fit_baseline(actor, data):
    _, targets = prepare_critic_data(actor, data['states_by_episode'], data['returns_by_episode'])
    lengths = [len(ep) for ep in data['states_by_episode']]
    constant, table, counts = fit_timestep_baseline([ep[:, 0].tolist() for ep in targets.split(lengths)])
    return table, constant, counts


def refresh_critic(critic, optimizer, data, table, constant, steps=CRITIC_STEPS):
    states, _, _, targets, _ = residual_dataset(critic, data, table, constant)
    critic.train().requires_grad_(True)
    with torch.no_grad():
        before = torch.nn.functional.mse_loss(critic(states), targets).item()
    for _ in range(steps):
        optimizer.zero_grad(set_to_none=True)
        loss = torch.nn.functional.mse_loss(critic(states), targets)
        if not torch.isfinite(loss): raise RuntimeError('Nonfinite critic loss')
        loss.backward()
        if any(p.grad is None or not torch.isfinite(p.grad).all() for p in critic.parameters()):
            raise RuntimeError('Nonfinite critic gradient')
        optimizer.step()
    optimizer.zero_grad(set_to_none=True)
    critic.eval().requires_grad_(False)
    with torch.no_grad():
        after = torch.nn.functional.mse_loss(critic(states), targets).item()
    return {'fit_mse_before': before, 'fit_mse_after': after, 'critic_steps_this_round': steps,
            'critic_optimizer_steps': int(next(iter(optimizer.state.values()))['step'].item())}


def actor_gradient(actor, data, table, constant, episode_limit, critic=None):
    states, targets = prepare_critic_data(actor, data['states_by_episode'], data['returns_by_episode'])
    lengths = [len(ep) for ep in data['states_by_episode']]
    if any(len(a) != len(s) for a, s in zip(data['latent_actions_by_episode'], data['states_by_episode'])):
        raise ValueError('Every state requires its original latent sample')
    latents = torch.stack([v for ep in data['latent_actions_by_episode'] for v in ep])
    base = torch.tensor([table[t] if t < len(table) else constant for length in lengths for t in range(length)],
                        dtype=targets.dtype).reshape(-1, 1)
    with torch.no_grad():
        corrections = critic(states) if critic is not None else torch.zeros_like(targets)
        advantages = ((targets - (base + corrections)) * CRITIC_RETURN_SCALE / episode_limit).flatten()
    actor.eval().requires_grad_(True)
    try:
        log_probs = torch.distributions.Normal(actor(states), actor.exploration_std).log_prob(latents).sum(dim=1)
        loss = -(log_probs * advantages.detach()).sum() / len(lengths)
        values = torch.autograd.grad(loss, tuple(actor.parameters()))
        gradient = torch.cat([v.detach().flatten() for v in values])
        if not torch.isfinite(loss) or not torch.isfinite(gradient).all():
            raise RuntimeError('Nonfinite actor gradient')
    finally:
        actor.requires_grad_(False)
    return gradient, {'loss': loss.item(), 'gradient_norm': gradient.double().norm().item(),
                      'mean_absolute_advantage': advantages.abs().mean().item(),
                      'fallback_timesteps': sum(max(0, n - len(table)) for n in lengths),
                      'gradient_timesteps': len(states)}


def step_actor(actor, optimizer, gradient, expected_update):
    before = snapshot(actor)
    if gradient.numel() != sum(p.numel() for p in actor.parameters()) or not torch.isfinite(gradient).all():
        raise ValueError('Finite complete actor gradient required')
    actor.requires_grad_(True)
    offset = 0
    for p in actor.parameters():
        p.grad = gradient[offset:offset + p.numel()].reshape_as(p).clone()
        offset += p.numel()
    optimizer.step()
    if any(int(state['step'].item()) != expected_update for state in optimizer.state.values()):
        raise RuntimeError('Actor optimizer state did not advance by one')
    optimizer.zero_grad(set_to_none=True)
    actor.requires_grad_(False)
    return maximum_change(actor, before)


def evaluate_and_save(actor, optimizer, seed, method, update, directory, initial=None):
    prefix = f'seed {seed} / {method} / update {update}'
    summary, data = evaluate_stochastic(actor, evaluation_seeds(seed),
                                        progress=lambda i, n: print(f'{prefix}: evaluation {i}/{n}', flush=True))
    summary['deterministic_survival_steps'] = evaluate_policy(AuraEnv(), actor)
    summary['update'] = update
    if initial is not None:
        summary['paired_changes'] = {}
        for metric, key in (('reward', 'episode_rewards'), ('discounted_return', 'episode_discounted_returns'),
                            ('survival_steps', 'episode_survival_steps')):
            delta = [a - b for a, b in zip(summary[key], initial[key])]
            summary['paired_changes'][metric] = {'mean': statistics.mean(delta), 'episode_differences': delta,
                                                 'sample_standard_deviation': statistics.stdev(delta),
                                                 'standard_error': statistics.stdev(delta) / 8}
    latest = directory / 'actor.pt'
    save_checkpoint(actor, optimizer, update * GRADIENT_EPISODES, latest, training_seed=seed,
                    training_run_id=f'step121-{seed}-{method}')
    saved = torch.load(latest, map_location='cpu', weights_only=True)
    saved.update(diagnostic_experiment='step121', evaluation=summary,
                 physical_action_schema_version=aura_python.action_schema_version)
    _write_checkpoint(saved, latest)
    _write_checkpoint(saved, directory / f'actor-update-{update}.pt')
    best = directory / 'actor_best.pt'
    previous = torch.load(best, map_location='cpu', weights_only=True) if best.exists() else None
    score = summary['stochastic_discounted_return']['mean']
    if previous is None or score > previous['best_evaluation_discounted_return']:
        saved.update(best_evaluation_discounted_return=score,
                     best_selection='Mean stochastic discounted return on 64 fixed monitoring seeds')
        _write_checkpoint(saved, best)
    # Keep raw evaluation experience for auditing; never train on it.
    torch.save({'kind': 'held_out_stochastic_evaluation', 'episode_seeds': list(evaluation_seeds(seed)), 'data': data},
               directory / f'evaluation-{update}.pt')
    (directory / f'evaluation-{update}.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(
        f"{prefix}: reward {summary['stochastic_reward']['mean']:.4f}; discounted {score:.4f}; deterministic {summary['deterministic_survival_steps']}",
        flush=True)
    return summary


def run_seed(seed, output, reference_dir):
    # Three independent experiment processes; the production collector is untouched.
    torch.set_num_threads(1)
    reference_actor, _, _, _, _ = load_reference(reference_dir)
    with torch.random.fork_rng(devices=[]):
        torch.manual_seed(seed)
        initial_actor = AuraPolicy(205).eval().requires_grad_(False)
        torch.manual_seed(43)
        critic = AuraValue(205).eval().requires_grad_(False)
    if seed == 42 and maximum_change(initial_actor, snapshot(reference_actor)) != 0:
        raise RuntimeError('Seed-42 initialization differs from the archived reference')
    actors = {method: copy.deepcopy(initial_actor) for method in METHODS}
    initial_difference = maximum_change(actors['residual'], snapshot(actors['timestep']))
    if initial_difference: raise RuntimeError('Initial actor copies differ')
    optimizers = {method: torch.optim.Adam(actor.parameters(), lr=RATE) for method, actor in actors.items()}
    critic_optimizer = torch.optim.Adam(critic.parameters(), lr=RATE)
    if any(optimizer.state for optimizer in optimizers.values()): raise RuntimeError('Initial Adam states not empty')
    seed_dir = Path(output) / f'seed-{seed}'
    seed_dir.mkdir(exist_ok=False)
    directories = {method: seed_dir / method for method in METHODS}
    for directory in directories.values(): directory.mkdir()
    records = {method: [] for method in METHODS}
    initial = evaluate_and_save(actors['timestep'], optimizers['timestep'], seed, 'timestep', 0,
                                directories['timestep'])
    # The actors and fixed evaluation streams are exactly identical at update 0.
    source = directories['timestep']
    target = directories['residual']
    for name in ('actor.pt', 'actor-update-0.pt', 'actor_best.pt'):
        saved = torch.load(source / name, map_location='cpu', weights_only=True)
        saved['training_run_id'] = f'step121-{seed}-residual'
        _write_checkpoint(saved, target / name)
    (target / 'evaluation-0.json').write_text(json.dumps(initial, indent=2) + '\n')
    records['timestep'].append(initial)
    records['residual'].append(copy.deepcopy(initial))
    rounds = []
    for update in range(1, UPDATES + 1):
        plan = streams(seed, update)
        round_report = {'update': update, 'streams': plan, 'methods': {}}
        for method in METHODS:
            actor = actors[method]
            before = snapshot(actor)
            directory = directories[method]

            def collect(stage):
                callback = lambda i, n, steps: print(f'seed {seed} / {method} / update {update}: {stage} {i}/{n}',
                                                     flush=True) if i % 8 == 0 else None
                return collect_gradient_rollouts(AuraEnv(), actor,
                                                 FIT_EPISODES if stage == 'fit' else GRADIENT_EPISODES,
                                                 plan[stage], callback)

            fit_data = collect('fit')
            table, constant, counts = fit_baseline(actor, fit_data)
            fit_info = {'timestep_table': table, 'constant_fallback': constant, 'timestep_counts': counts}
            if method == 'residual':
                fit_info.update(refresh_critic(critic, critic_optimizer, fit_data, table, constant))
                _write_checkpoint({'kind': 'aura_value_residual', 'model': critic.state_dict(),
                                   'optimizer': critic_optimizer.state_dict(),
                                   'observation_size': 205, 'observation_schema_version': OBSERVATION_SCHEMA_VERSION,
                                   'action_schema_version': ACTION_SCHEMA_VERSION,
                                   'physical_action_schema_version': aura_python.action_schema_version,
                                   'baseline': fit_info, 'source_actor_update': update - 1, 'actor_seed': seed},
                                  directory / f'critic-round-{update}.pt')
            if maximum_change(actor, before): raise RuntimeError('Baseline fitting changed the actor')
            gradient_data = collect('gradient')
            critic_before = snapshot(critic)
            gradient, details = actor_gradient(actor, gradient_data, table, constant, 1200,
                                               critic if method == 'residual' else None)
            change = step_actor(actor, optimizers[method], gradient, update)
            if maximum_change(critic, critic_before) or any(p.grad is not None for p in critic.parameters()):
                raise RuntimeError('Actor update changed the frozen critic')
            torch.save({'kind': 'separate_on_policy_fit_and_gradient_batches', 'fit': fit_data,
                        'gradient_batch': gradient_data,
                        'actor_gradient': gradient, 'parameter_layout': parameter_layout(actor), 'baseline': fit_info,
                        'streams': plan},
                       directory / f'training-round-{update}.pt')
            del fit_data, gradient_data
            evaluation = evaluate_and_save(actor, optimizers[method], seed, method, update, directory, initial)
            records[method].append(evaluation)
            round_report['methods'][method] = {**details, 'maximum_parameter_change_this_update': change,
                                               'fit': fit_info, 'evaluation': evaluation,
                                               'baseline_fit_disjoint_from_actor_gradient_batch': True,
                                               'critic_frozen_during_actor_update': True}
        rounds.append(round_report)
        (seed_dir / 'progress.json').write_text(
            json.dumps({'actor_seed': seed, 'completed_updates': update, 'records': records, 'rounds': rounds},
                       indent=2) + '\n')
    paired = []
    for update in range(4):
        row = {'update': update}
        for metric, key in (('reward', 'episode_rewards'), ('discounted_return', 'episode_discounted_returns'),
                            ('survival_steps', 'episode_survival_steps')):
            delta = [a - b for a, b in zip(records['residual'][update][key], records['timestep'][update][key])]
            row[metric] = {'mean_critic_minus_timestep': statistics.mean(delta), 'episode_differences': delta,
                           'sample_standard_deviation': statistics.stdev(delta)}
        paired.append(row)
    result = {'actor_seed': seed, 'records': records, 'rounds': rounds, 'paired_method_comparisons': paired,
              'initial_actor_copies_identical': True, 'initial_actor_maximum_difference': initial_difference,
              'initial_adam_states_empty': True,
              'actor_updates_per_method': 3, 'critic_optimizer_steps': 60, 'evaluation_used_for_training': False,
              'update_zero_evaluation_shared': True, 'production_trainer_changed': False}
    (seed_dir / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-dir', type=Path, default=Path('models/critics/step115-current-physics'))
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--workers', type=int, choices=(1, 2, 3), default=3)
    args = parser.parse_args()
    if args.output_dir.exists(): parser.error('Choose an unused experiment directory')
    _, _, _, _, reference = load_reference(args.reference_dir)
    protected = {str(p): digest(p) for p in args.reference_dir.iterdir() if p.is_file()}
    for step, name in ((118, 'actor-updates'), (119, 'stochastic-evaluation'), (120, 'paired-bootstrap')):
        path = Path(f'models/experiments/step{step}-{name}') / 'report.json'
        protected[str(path)] = digest(path)
    config = {'experiment': 'step121-consecutive-actor-updates', 'actor_seeds': list(ACTOR_SEEDS),
              'actor_updates': UPDATES,
              'actor_architecture': [205, 64, 64, 32], 'action_schema': 5, 'observation_schema': 2,
              'physical_action_schema': 4,
              'episodes_per_gradient_batch': GRADIENT_EPISODES, 'separate_fit_episodes_per_round': FIT_EPISODES,
              'critic_steps_per_round': CRITIC_STEPS, 'critic_initialization_seed': 43,
              'critic_initialization': 'Fresh critic per actor seed; warm-start weights and Adam moments across rounds',
              'actor_optimizer': 'Adam, persistent moments across updates', 'learning_rate': RATE,
              'critic_learning_rate': RATE, 'discount_factor': DISCOUNT_FACTOR, 'return_scale': CRITIC_RETURN_SCALE,
              'actor_advantage_denominator': 1200, 'loss_reduction': 'Negative sum over timesteps / 32 episodes',
              'exploration_std': .25, 'sample_bounds': [-1., 1.], 'evaluation_episodes_per_policy': 64,
              'evaluation_seeds': {str(seed): list(evaluation_seeds(seed)) for seed in ACTOR_SEEDS},
              'training_streams': {str(seed): {str(u): streams(seed, u) for u in range(1, 4)} for seed in ACTOR_SEEDS},
              'best_selection': 'Mean stochastic discounted return; ties retain earlier checkpoint',
              'initial_evaluation_reused_between_identical_methods': True, 'expected_simulated_episodes': 2496,
              'parallel_seed_processes': args.workers, 'torch_threads_per_process': 1,
              'torch_version': str(torch.__version__),
              'physics_extension_sha256': reference['physics_extension_sha256'],
              'protected_artifacts_sha256': protected, 'source_sha256': digest(__file__),
              'production_trainer_changed': False}
    args.output_dir.mkdir(parents=True, exist_ok=False)
    (args.output_dir / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
    (args.output_dir / 'experiment.py').write_bytes(Path(__file__).read_bytes())
    with ProcessPoolExecutor(max_workers=args.workers, mp_context=multiprocessing.get_context('spawn')) as pool:
        futures = [pool.submit(run_seed, seed, str(args.output_dir), str(args.reference_dir)) for seed in ACTOR_SEEDS]
        results = [future.result() for future in futures]
    if any(digest(path) != expected for path, expected in protected.items()):
        raise RuntimeError('A protected reference artifact changed')
    summary = []
    for update in range(4):
        row = {'update': update, 'methods': {}}
        for method in METHODS:
            evaluations = [result['records'][method][update] for result in results]
            row['methods'][method] = {
                'mean_stochastic_reward': statistics.mean(e['stochastic_reward']['mean'] for e in evaluations),
                'mean_stochastic_discounted_return': statistics.mean(
                    e['stochastic_discounted_return']['mean'] for e in evaluations),
                'mean_stochastic_survival_steps': statistics.mean(
                    e['stochastic_survival_steps']['mean'] for e in evaluations),
                'mean_deterministic_survival_steps': statistics.mean(
                    e['deterministic_survival_steps'] for e in evaluations),
                'mean_reward_change_from_initial': statistics.mean(
                    result['records'][method][update]['stochastic_reward']['mean'] -
                    result['records'][method][0]['stochastic_reward']['mean'] for result in results)}
        row['per_actor_seed_reward_difference'] = {
            str(result['actor_seed']): result['paired_method_comparisons'][update]['reward'][
                'mean_critic_minus_timestep'] for result in results}
        summary.append(row)
    report = {**config, 'actor_seed_results': results, 'summary_by_update': summary,
              'protected_artifacts_unchanged': True,
              'limitations': 'Only three actor initializations, three updates and one fixed starting posture. Evaluation seeds are reused across updates as a monitoring set; best-policy scores have selection bias. Latest-policy comparisons are primary. Do not treat episodes or consecutive updates as independent actor seeds.',
              'artifacts': {str(p.relative_to(args.output_dir)): digest(p) for p in args.output_dir.rglob('*') if
                            p.is_file()}}
    (args.output_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
