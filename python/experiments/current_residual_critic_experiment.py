"""Step 115: fixed-budget residual critic experiment on current physics.

Train on 32 whole trajectories; collect and inspect 16 independent test trajectories
only after all 20 updates. Production REINFORCE and saved actors are untouched.
"""
import argparse
import aura_python
import hashlib
import json
import math
import subprocess
import torch
from pathlib import Path

from python.aura_env import LegacyAuraEnv as AuraEnv, OBSERVATION_SCHEMA_VERSION, OBSERVATION_SIZE
from python.aura_policy import AuraPolicy, AuraValue, ACTION_SCHEMA_VERSION, CONTROLLED_ACTION_KEYS
from python.experiments.residual_critic_experiment import residual_dataset, evaluate_residual
from python.experiments.timestep_baseline_analysis import fit_timestep_baseline
from python.train_nn import CRITIC_RETURN_SCALE, DISCOUNT_FACTOR, collect_critic_rollouts, prepare_critic_data

CONFIG = dict(actor_seed=42, critic_seed=43, training_exploration_seed=11501,
              test_exploration_seed=11502, training_episodes=32, test_episodes=16,
              critic_updates=20, learning_rate=.001, discount_factor=DISCOUNT_FACTOR,
              return_scale=CRITIC_RETURN_SCALE, actor_optimizer_updates=0)


def stage_metrics(bins):
    """Aggregate timestep-weighted errors without mixing observations between splits."""
    stages = []
    for name, indices in (("early / 0–119", (0, 1)), ("middle / 120–239", (2, 3)), ("late / 240+", (4, 5))):
        selected = [bins[i] for i in indices]
        count = sum(row['count'] for row in selected)
        row = {'stage': name, 'count': count}
        for field in ('timestep_mse', 'combined_mse'):
            row[field] = math.fsum(
                part[field] * part['count'] for part in selected if part['count']) / count if count else None
        stages.append(row)
    return stages


def run_current_experiment(*, env_factory=AuraEnv, progress=None, measurement=None):
    """One preregistered configuration; no test-set measurement during training."""
    training_env = env_factory()
    with torch.random.fork_rng(devices=[]):
        torch.manual_seed(CONFIG['actor_seed'])
        actor = AuraPolicy(training_env.observation_size).eval().requires_grad_(False)
        torch.manual_seed(CONFIG['critic_seed'])
        critic = AuraValue(training_env.observation_size)
    actor_before = [p.detach().clone() for p in actor.parameters()]
    critic_before = [p.detach().clone() for p in critic.parameters()]

    def collect(env, name):
        callback = (lambda episode, total, steps: progress(name, episode, total, steps)) if progress else None
        return collect_critic_rollouts(env, actor, CONFIG[f'{name}_episodes'],
                                       CONFIG[f'{name}_exploration_seed'], callback)

    training = collect(training_env, 'training')
    _, targets = prepare_critic_data(critic, training['states_by_episode'], training['returns_by_episode'])
    lengths = [len(ep) for ep in training['states_by_episode']]
    constant, table, counts = fit_timestep_baseline([ep[:, 0].tolist() for ep in targets.split(lengths)])
    train = residual_dataset(critic, training, table, constant)
    optimizer = torch.optim.Adam(critic.parameters(), lr=CONFIG['learning_rate'])
    measurements = []

    def measure(update):
        critic.eval()
        metrics = evaluate_residual(critic, train)
        row = {'update': update, 'training_residual_mse': metrics['combined_mse']}
        measurements.append(row)
        if measurement: measurement(row)

    measure(0)
    for update in range(1, CONFIG['critic_updates'] + 1):
        critic.train()
        optimizer.zero_grad(set_to_none=True)
        predictions = critic(train[0])
        if predictions.shape != train[3].shape:
            raise ValueError('Residual predictions and targets must match [N, 1]')
        loss = torch.nn.functional.mse_loss(predictions, train[3])
        if not torch.isfinite(loss): raise RuntimeError('Nonfinite residual loss')
        loss.backward()
        if any(p.grad is not None and not torch.isfinite(p.grad).all() for p in critic.parameters()):
            raise RuntimeError('Nonfinite residual gradients')
        optimizer.step()
        if update in (1, 5, 10, 20): measure(update)
    # Lock the final critic before creating the untouched test split. No callback
    # sees test errors until this fixed final model has finished all its updates.
    critic.eval().requires_grad_(False)
    test = collect(env_factory(), 'test')
    prepared = residual_dataset(critic, test, table, constant)
    test_metrics = evaluate_residual(critic, prepared, len(table))
    actor_change = max((p.detach() - old).abs().max().item() for p, old in zip(actor.parameters(), actor_before))
    no_actor_gradients = all(p.grad is None for p in actor.parameters())
    if actor_change != 0 or not no_actor_gradients: raise RuntimeError('Frozen actor was changed')
    baseline = test_metrics['timestep_mse']
    combined = test_metrics['combined_mse']
    report = {**CONFIG, 'optimizer': 'Adam', 'observation_size': training_env.observation_size,
              'action_count': len(actor.action_transforms), 'observation_schema': OBSERVATION_SCHEMA_VERSION,
              'action_schema': ACTION_SCHEMA_VERSION, 'physical_action_schema': aura_python.action_schema_version,
              'actor_architecture': [training_env.observation_size, 64, 64, len(actor.action_transforms)],
              'critic_architecture': [training_env.observation_size, 64, 64, 1],
              'episode_limit': training_env.episode_limit,
              'training_survival_steps': training['episode_scores'], 'test_survival_steps': test['episode_scores'],
              'training_timesteps': len(train[0]), 'test_timesteps': len(prepared[0]),
              'training_raw_return_range': [min(v for ep in training['returns_by_episode'] for v in ep),
                                            max(v for ep in training['returns_by_episode'] for v in ep)],
              'test_raw_return_range': [min(v for ep in test['returns_by_episode'] for v in ep),
                                        max(v for ep in test['returns_by_episode'] for v in ep)],
              'constant_scaled_prediction': constant, 'timestep_scaled_predictions': table,
              'training_observations_per_timestep': counts, 'training_measurements': measurements,
              'test_evaluations': 1, 'test_used_for_training': False,
              'checkpoint_selection': 'Fixed final update 20; test collected afterward',
              'timestep_test_mse': baseline, 'combined_test_mse': combined,
              'relative_improvement_percent': 100 * (baseline - combined) / baseline if baseline else None,
              'test_bins': test_metrics['bins'], 'test_stages': stage_metrics(test_metrics['bins']),
              'test_coverage': test_metrics['coverage'],
              'test_fallback_count': test_metrics['coverage']['uncovered_count'],
              'test_fallback_percent': 100 * test_metrics['coverage']['uncovered_count'] / len(prepared[0]),
              'constant_test_mse': torch.mean((prepared[1].double() - constant) ** 2).item(),
              'actor_max_parameter_change': actor_change, 'actor_has_no_gradients': no_actor_gradients,
              'critic_max_parameter_change': max(
                  (p - old).abs().max().item() for p, old in zip(critic.parameters(), critic_before)),
              'environment_steps': sum(training['episode_scores']) + sum(test['episode_scores']),
              'production_actor_baseline_changed': False, 'device': 'cpu', 'torch_version': str(torch.__version__),
              'physics_extension_sha256': hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest(),
              'units': 'Squared discounted-return units after division by 100; weighted by timestep',
              'fallback': 'Training mean scaled return beyond the longest training trajectory',
              'limitations': 'One fixed actor seed and 16 test trajectories. Current body has 32 actions, versus 14 historically; cross-physics absolute MSE is not a controlled physics-only comparison. Test results must not be used to tune this model. Prediction accuracy does not establish gradient or balance improvement.'}
    checkpoint = {'kind': 'aura_value_residual', 'format_version': 1, 'model': critic.state_dict(),
                  'optimizer': optimizer.state_dict(), 'return_scale': CRITIC_RETURN_SCALE,
                  'discount_factor': DISCOUNT_FACTOR,
                  'observation_size': training_env.observation_size,
                  'observation_schema_version': OBSERVATION_SCHEMA_VERSION,
                  'action_schema_version': ACTION_SCHEMA_VERSION,
                  'physical_action_schema_version': aura_python.action_schema_version,
                  'baseline': {'constant': constant, 'table': table, 'training_counts': counts}, 'experiment': report}
    frozen_actor = {'kind': 'frozen_actor_snapshot', 'model': actor.state_dict(),
                    'observation_size': training_env.observation_size,
                    'observation_schema_version': OBSERVATION_SCHEMA_VERSION,
                    'action_schema_version': ACTION_SCHEMA_VERSION,
                    'physical_action_schema_version': aura_python.action_schema_version,
                    'action_transforms': actor.action_transforms, 'seed': CONFIG['actor_seed']}
    return report, checkpoint, training, test, frozen_actor


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output_dir.resolve()
    if output.exists(): parser.error('Choose an unused experiment directory; prior results are preserved')
    # Snapshot existing research artifacts before writing only to this new directory.
    protected = sorted(set((root / 'docs').glob('critic-step*.json')) |
                       set((root / 'models').rglob('*.pt')) | set((root / 'models').rglob('report.json')))
    hashes = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in protected}
    source_paths = sorted(path for folder in ('include', 'src', 'python') for path in (root / folder).rglob('*')
                          if path.suffix in ('.hpp', '.cpp', '.py'))
    sources = {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths}
    old = json.loads((root / 'docs/research/results/critic-step114.json').read_text())
    previous_baseline = old['timestep_validation_mse']
    previous_combined = old['measurements'][-1]['validation_combined_mse']
    config = {**CONFIG, 'experiment': 'step115-current-physics-fixed20',
              'test_policy': 'Collect and evaluate only after update 20; no checkpoint selection',
              'observation_size': OBSERVATION_SIZE, 'action_count': len(CONTROLLED_ACTION_KEYS),
              'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
              'source_sha256': sources, 'preserved_artifact_sha256': hashes,
              'physics_extension_sha256': hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest()}
    output.mkdir(parents=True, exist_ok=False)
    (output / 'config.json').write_text(json.dumps(config, indent=2) + '\n')

    def progress(split, episode, total, steps):
        print(f'{split.title()} episode {episode}/{total}: {steps} steps', flush=True)

    def measurement(row):
        print(f"Critic update {row['update']}/20: training residual MSE {row['training_residual_mse']:.9f}", flush=True)

    report, checkpoint, training, test, actor = run_current_experiment(progress=progress, measurement=measurement)
    for filename, dataset in (('training.pt', training), ('test.pt', test)):
        with (output / filename).open('xb') as stream: torch.save(dataset, stream)
    for filename, data in (('critic.pt', checkpoint), ('frozen-actor.pt', actor)):
        with (output / filename).open('xb') as stream: torch.save(data, stream)
    report['previous_physics'] = {'timestep_mse': previous_baseline, 'combined_mse': previous_combined,
                                  'relative_improvement_percent': 100 * (
                                              previous_baseline - previous_combined) / previous_baseline}
    report['provenance'] = config
    report['artifacts'] = {filename: {'path': str(output / filename),
                                      'sha256': hashlib.sha256((output / filename).read_bytes()).hexdigest()}
                           for filename in ('training.pt', 'test.pt', 'critic.pt', 'frozen-actor.pt')}
    if any(hashlib.sha256((root / path).read_bytes()).hexdigest() != digest for path, digest in hashes.items()):
        raise RuntimeError('A pre-existing research artifact changed')
    if any(hashlib.sha256((root / path).read_bytes()).hexdigest() != digest for path, digest in sources.items()):
        raise RuntimeError('Experiment source changed during collection')
    if hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest() != config['physics_extension_sha256']:
        raise RuntimeError('Physics extension changed during collection')
    report['previous_artifacts_unchanged'] = True
    with (output / 'report.json').open('x') as stream:
        json.dump(report, stream, indent=2, allow_nan=False);stream.write('\n')
    print(f"Test timestep MSE: {report['timestep_test_mse']:.9f}", flush=True)
    print(f"Test combined MSE: {report['combined_test_mse']:.9f}", flush=True)
    print(
        f"Relative improvement: {report['relative_improvement_percent']:+.2f}% / actor change {report['actor_max_parameter_change']}",
        flush=True)
    print(f'Report: {output / "report.json"}', flush=True)


if __name__ == '__main__': main()
