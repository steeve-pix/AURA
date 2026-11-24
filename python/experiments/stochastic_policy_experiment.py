"""Step 119: held-out stochastic evaluation of the archived Step 118 actors.

No optimizer, gradients or training. Episode seeds restart independently for
all policies: different termination lengths cannot shift later random streams.
"""
import argparse
import json
import math
import statistics
import torch
from pathlib import Path

from python.experiments.actor_update_experiment import digest, snapshot, maximum_change
from python.aura_env import LegacyAuraEnv as AuraEnv
from python.model_checkpoint import load_checkpoint
from python.experiments.residual_gradient_experiment import load_reference, collect_gradient_rollouts, METHODS
from python.train_nn import DISCOUNT_FACTOR

EVALUATION_SEEDS = tuple(range(119001, 119033))
FORBIDDEN_SEEDS = {11501, 11502, 11601, 11602, 11603, 11604, 11605,
                   11701, 11802, 11803, 11804, 11805}


def summarize_episodes(data, seeds):
    rewards = [math.fsum(episode) for episode in data['rewards_by_episode']]
    discounted = [episode[0] for episode in data['returns_by_episode']]
    survival = data['episode_scores']

    def stats(values):
        return {'mean': statistics.mean(values), 'sample_standard_deviation': statistics.stdev(values)}

    return {'episodes': len(seeds), 'episode_seeds': list(seeds),
            'episode_rewards': rewards, 'episode_discounted_returns': discounted,
            'episode_survival_steps': survival,
            'stochastic_reward': stats(rewards), 'stochastic_discounted_return': stats(discounted),
            'stochastic_survival_steps': stats(survival)}


def evaluate_stochastic(actor, seeds=EVALUATION_SEEDS, *, env_factory=AuraEnv, progress=None):
    if len(seeds) < 2 or len(set(seeds)) != len(seeds) or set(seeds) & FORBIDDEN_SEEDS:
        raise ValueError('At least two distinct, held-out episode seeds required')
    actor.eval().requires_grad_(False)
    if any(p.grad is not None for p in actor.parameters()):
        raise ValueError('Evaluation actor must start without gradients')
    before = snapshot(actor)
    env = env_factory()
    data = None
    for index, seed in enumerate(seeds, 1):
        episode = collect_gradient_rollouts(env, actor, 1, seed)
        if data is None:
            data = {key: [] for key in episode}
        for key, values in episode.items():
            data[key].extend(values)
        if progress and (index % 8 == 0 or index == len(seeds)):
            progress(index, len(seeds))
    change = maximum_change(actor, before)
    if change or any(p.grad is not None for p in actor.parameters()):
        raise RuntimeError('Evaluation changed actor weights or accumulated gradients')
    summary = summarize_episodes(data, seeds)
    summary.update(actor_maximum_parameter_change=change, actor_has_no_gradients=True, optimizer_updates=0)
    return summary, data


def paired_change(updated, initial, key):
    differences = [a - b for a, b in zip(updated[key], initial[key])]
    if len(differences) != 32 or updated['episode_seeds'] != initial['episode_seeds']:
        raise ValueError('Comparison requires the same 32 episode streams')
    return {'mean': statistics.mean(differences),
            'sample_standard_deviation': statistics.stdev(differences),
            'standard_error': statistics.stdev(differences) / math.sqrt(len(differences)),
            'episode_differences': differences}


def compare_results(initial, updated, batches):
    rows = []
    for batch in batches:
        row = {'batch': batch['batch'], 'initial': initial, 'methods': {}}
        for method in METHODS:
            result = updated[f"{batch['batch']}-{method}"]
            result = dict(result)
            result['deterministic_survival_steps'] = batch['methods'][method]['survival_after_update_steps']
            result['deterministic_change_steps'] = batch['methods'][method]['change_from_initial_steps']
            for label, key in (('reward_change', 'episode_rewards'),
                               ('discounted_return_change', 'episode_discounted_returns'),
                               ('survival_change', 'episode_survival_steps')):
                result[label] = paired_change(result, initial, key)
            row['methods'][method] = result
        rows.append(row)
    summary = {}
    for method in METHODS:
        results = [row['methods'][method] for row in rows]
        summary[method] = {
            'mean_reward_change': statistics.mean(r['reward_change']['mean'] for r in results),
            'mean_discounted_return_change': statistics.mean(r['discounted_return_change']['mean'] for r in results),
            'mean_stochastic_survival_change_steps': statistics.mean(r['survival_change']['mean'] for r in results),
            'batches_with_improved_reward': sum(r['reward_change']['mean'] > 0 for r in results),
            'batches_with_improved_discounted_return': sum(r['discounted_return_change']['mean'] > 0 for r in results),
        }
    return rows, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-dir', type=Path, default=Path('models/critics/step115-current-physics'))
    parser.add_argument('--step118-dir', type=Path, default=Path('models/experiments/step118-actor-updates'))
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.exists():
        parser.error('Choose an unused output directory; archived policies are read-only')
    initial, critic, _, _, reference = load_reference(args.reference_dir)
    critic_before = snapshot(critic)
    previous = json.loads((args.step118_dir / 'report.json').read_text())
    protected = {str(args.step118_dir / 'report.json'): digest(args.step118_dir / 'report.json')}
    for name, expected in previous['artifacts'].items():
        path = args.step118_dir / name
        if digest(path) != expected:
            raise ValueError(f'Step 118 artifact changed: {path}')
        protected[str(path)] = expected
    for name, expected in previous['reference_artifacts_sha256'].items():
        if digest(name) != expected:
            raise ValueError(f'Step 115 artifact changed: {name}')
        protected[name] = expected
    original_report = Path(previous['reused_batch_a']) / 'report.json'
    if digest(original_report) != previous['reused_report_sha256']:
        raise ValueError('Reused Step 117 report changed')
    protected[str(original_report)] = digest(original_report)
    original = json.loads(original_report.read_text())
    for name, expected in original['artifacts'].items():
        path = original_report.parent / name
        if digest(path) != expected:
            raise ValueError(f'Step 117 artifact changed: {path}')
        protected[str(path)] = expected
    if (previous['actor_seed'] != 42 or previous['action_count'] != 32 or previous['action_schema'] != 5 or
            previous['observation_size'] != 205 or previous['discount_factor'] != DISCOUNT_FACTOR or
            previous['physics_extension_sha256'] != reference['physics_extension_sha256'] or
            len(previous['batches']) != 5):
        raise ValueError('Step 118 configuration differs from the current reference')
    actors = {'initial': initial}
    for batch in previous['batches']:
        for method in METHODS:
            path = Path(batch['methods'][method]['latest_checkpoint'])
            if str(path) not in protected:
                raise ValueError('Updated actor must be covered by a verified archive manifest')
            actor, saved = load_checkpoint(path, AuraEnv())
            if (saved['updates_completed'] != 1 or saved['episodes_completed'] != 32 or
                    actor.exploration_std != initial.exploration_std or actor.sample_bounds != initial.sample_bounds or
                    actor.action_transforms != initial.action_transforms):
                raise ValueError('Actor is not the controlled one-update policy')
            # load_checkpoint validates schema and exact layer dimensions; compare hidden architecture too.
            if str(actor.network) != str(initial.network):
                raise ValueError('Actor architecture differs from the initial policy')
            actors[f"{batch['batch']}-{method}"] = actor
    config = {'experiment': 'step119-matched-stochastic-policy-evaluation',
              'actor_seed': 42, 'observation_size': 205, 'action_count': 32, 'action_schema': 5,
              'episode_seeds': list(EVALUATION_SEEDS), 'episodes_per_policy': 32,
              'unique_policies': 11, 'total_evaluation_episodes': 352,
              'optimizer_updates': 0, 'critic_used': False,
              'discount_factor': DISCOUNT_FACTOR, 'exploration_std': initial.exploration_std,
              'sample_bounds': list(initial.sample_bounds),
              'reward_definition': 'Sum of raw balance rewards over the episode',
              'discounted_return_definition': 'Sum of gamma^t * reward_t from episode start; unscaled',
              'matching': 'Restart the same seed at each episode for every policy; trajectories may diverge',
              'initial_evaluation_reused_across_batches': True,
              'physics_extension_sha256': reference['physics_extension_sha256'],
              'protected_artifacts_sha256': protected, 'source_sha256': digest(__file__),
              'torch_version': str(torch.__version__), 'production_trainer_changed': False}
    args.output_dir.mkdir(parents=True, exist_ok=False)
    (args.output_dir / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
    (args.output_dir / 'experiment.py').write_bytes(Path(__file__).read_bytes())
    results = {}
    for label, actor in actors.items():
        print(f'Evaluating {label}', flush=True)
        result, data = evaluate_stochastic(actor, progress=lambda i, n: print(f'  {i}/{n} episodes', flush=True))
        result['policy'] = label
        if label == 'initial':
            result['deterministic_survival_steps'] = previous['batches'][0]['methods']['timestep'][
                'initial_deterministic_survival_steps']
        results[label] = result
        torch.save({'kind': 'held_out_stochastic_evaluation', 'episode_seeds': list(EVALUATION_SEEDS), 'data': data},
                   args.output_dir / f'{label}.pt')
        (args.output_dir / f'{label}.json').write_text(json.dumps(result, indent=2) + '\n')
        print(
            f"  Reward {result['stochastic_reward']['mean']:.6f}; discounted {result['stochastic_discounted_return']['mean']:.6f}; survival {result['stochastic_survival_steps']['mean']:.3f}",
            flush=True)
    rows, summary = compare_results(results['initial'], results, previous['batches'])
    if maximum_change(critic, critic_before) or any(digest(path) != expected for path, expected in protected.items()):
        raise RuntimeError('Evaluation changed the critic or a protected archive')
    report = {**config, 'batches': rows, 'summary': summary, 'critic_maximum_parameter_change': 0.,
              'protected_artifacts_unchanged': True,
              'limitations': '32 matched episode streams, one actor initialization and one fixed physical reset. Initial evaluation is shared, so batch changes are correlated. These episodes are held out and must not be used for optimizer updates or checkpoint promotion.',
              'artifacts': {str(path.relative_to(args.output_dir)): digest(path) for path in args.output_dir.iterdir()
                            if path.is_file()}}
    (args.output_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
