"""Step 120: paired, dependence-preserving bootstrap of saved Step 119 results.

Standard-library analysis only: never imports the simulator, neural models or
optimizers. Percentile intervals are pointwise, exploratory 95% intervals.
"""
import argparse
import hashlib
import json
import math
import random
import statistics
from pathlib import Path

METRICS = {'reward': 'episode_rewards', 'discounted_return': 'episode_discounted_returns',
           'survival_steps': 'episode_survival_steps'}
CONTRASTS = ('timestep_minus_initial', 'residual_minus_initial', 'residual_minus_timestep')
RESAMPLES = 10000
BOOTSTRAP_SEED = 12042


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def quantile(sorted_values, probability):
    position = (len(sorted_values) - 1) * probability
    low = math.floor(position)
    high = math.ceil(position)
    return sorted_values[low] + (position - low) * (sorted_values[high] - sorted_values[low])


def interval(values, draws):
    estimates = sorted(math.fsum(values[i] for i in indices) / len(indices) for indices in draws)
    return {'mean_difference': statistics.mean(values),
            'confidence_interval_95': [quantile(estimates, .025), quantile(estimates, .975)]}


def paired_vectors(initial, policies):
    seeds = initial['episode_seeds']
    if len(seeds) != 32 or len(set(seeds)) != 32:
        raise ValueError('Exactly 32 distinct evaluation-seed pairs required')
    for policy in [initial, *policies.values()]:
        if policy['episode_seeds'] != seeds:
            raise ValueError('Evaluation seed positions must match exactly')
        if any(len(policy[key]) != 32 or not all(math.isfinite(value) for value in policy[key])
               for key in METRICS.values()):
            raise ValueError('Finite aligned per-episode metrics required')
    result = {}
    for metric, key in METRICS.items():
        initial_values = initial[key]
        timestep = policies['timestep'][key]
        residual = policies['residual'][key]
        result[metric] = {
            'timestep_minus_initial': [a - b for a, b in zip(timestep, initial_values)],
            'residual_minus_initial': [a - b for a, b in zip(residual, initial_values)],
            'residual_minus_timestep': [a - b for a, b in zip(residual, timestep)],
        }
    return result


def bootstrap_analysis(initial, batches, *, resamples=RESAMPLES, seed=BOOTSTRAP_SEED):
    if len(batches) != 5 or resamples < 2:
        raise ValueError('Five updates and at least two bootstrap resamples required')
    vectors = {label: paired_vectors(initial, policies) for label, policies in batches.items()}
    labels = list(vectors)
    rng = random.Random(seed)
    # Reuse each seed-index draw across every policy, update and contrast.
    draws = [[rng.randrange(32) for _ in range(32)] for _ in range(resamples)]
    by_batch = {label: {metric: {contrast: interval(vectors[label][metric][contrast], draws)
                                 for contrast in CONTRASTS} for metric in METRICS} for label in labels}
    aggregate = {}
    crossed_rng = random.Random(seed + 1)
    crossed_draws = [([crossed_rng.randrange(5) for _ in range(5)],
                      [crossed_rng.randrange(32) for _ in range(32)]) for _ in range(resamples)]
    for metric in METRICS:
        aggregate[metric] = {}
        for contrast in CONTRASTS:
            matrix = [vectors[label][metric][contrast] for label in labels]
            # A seed is a cluster containing all five paired update results.
            seed_means = [statistics.mean(row[i] for row in matrix) for i in range(32)]
            conditional = interval(seed_means, draws)
            estimates = []
            # Independent row/column resampling preserves shared seed effects
            # while also exploring variability among the five sampled updates.
            for batch_indices, seed_indices in crossed_draws:
                column_means = [math.fsum(matrix[b][i] for b in batch_indices) / 5 for i in range(32)]
                estimates.append(math.fsum(column_means[i] for i in seed_indices) / 32)
            estimates.sort()
            aggregate[metric][contrast] = {
                'conditional_on_five_updates': conditional,
                'crossed_update_and_seed_bootstrap': {
                    'mean_difference': statistics.mean(seed_means),
                    'confidence_interval_95': [quantile(estimates, .025), quantile(estimates, .975)]},
                'per_seed_mean_differences': seed_means,
            }
    return {'per_batch': by_batch, 'aggregate': aggregate, 'paired_episode_differences': vectors,
            'resamples': resamples, 'bootstrap_seed': seed, 'crossed_bootstrap_seed': seed + 1,
            'interval_method': 'Percentile bootstrap, linear interpolation at 2.5% and 97.5%; pointwise, no multiplicity adjustment',
            'aggregate_method': 'Resample 32 evaluation-seed clusters jointly across all five updates; conditional on the five archived policies',
            'secondary_aggregate_method': 'Crossed bootstrap: independently resample five update rows and 32 shared evaluation-seed columns',
            'limitations': 'One actor initialization, one initial posture, 32 evaluation seeds and only five update batches. Conditional intervals exclude training-batch uncertainty; crossed intervals approximate it from only five rows. No actor-seed or posture generalization, no simultaneous coverage, and no correction for prior inspection of these results.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evaluation-dir', type=Path, default=Path('models/experiments/step119-stochastic-evaluation'))
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.exists():
        parser.error('Choose an unused directory; previous data must be preserved')
    source = args.evaluation_dir / 'report.json'
    reference = json.loads(source.read_text())
    protected = {str(source): digest(source)}
    for name, expected in reference['artifacts'].items():
        path = args.evaluation_dir / name
        if digest(path) != expected:
            raise ValueError(f'Step 119 artifact changed: {path}')
        protected[str(path)] = expected
    initial = json.loads((args.evaluation_dir / 'initial.json').read_text())
    batches = {label: {method: json.loads((args.evaluation_dir / f'{label}-{method}.json').read_text())
                       for method in ('timestep', 'residual')} for label in 'ABCDE'}
    config = {'experiment': 'step120-paired-bootstrap-analysis', 'bootstrap_seed': BOOTSTRAP_SEED,
              'resamples': RESAMPLES, 'confidence_level': .95, 'evaluation_seed_pairs': 32,
              'update_batches': 5, 'simulations_run': 0, 'optimizer_updates': 0,
              'protected_artifacts_sha256': protected, 'source_sha256': digest(__file__)}
    args.output_dir.mkdir(parents=True, exist_ok=False)
    (args.output_dir / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
    (args.output_dir / 'analysis.py').write_bytes(Path(__file__).read_bytes())
    report = bootstrap_analysis(initial, batches)
    if any(digest(path) != expected for path, expected in protected.items()):
        raise RuntimeError('Analysis modified protected evaluation data')
    report.update(config, protected_artifacts_unchanged=True)
    (args.output_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for label, metrics in report['per_batch'].items():
        print(label, json.dumps(metrics['reward']), flush=True)
    print('Aggregate:', json.dumps(report['aggregate'], indent=2), flush=True)


if __name__ == '__main__':
    main()
