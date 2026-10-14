"""Step 112: read-only temporal diagnostics for a saved Step 110 critic.

Replays the original validation exploration stream because Step 110 did not
persist observation tensors. Refuses mismatched physics, schemas or replay
results. Neither actor nor critic has an optimizer in this diagnostic.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import torch
import aura_python

from .aura_env import AuraEnv, OBSERVATION_SCHEMA_VERSION
from .aura_policy import AuraPolicy, AuraValue, ACTION_SCHEMA_VERSION
from .train_nn import CRITIC_RETURN_SCALE, DISCOUNT_FACTOR, collect_critic_rollouts, prepare_critic_data

BINS = ((0, 60), (60, 120), (120, 180), (180, 240), (240, 300), (300, None))


def temporal_metrics(predictions_by_episode, targets_by_episode, constant):
    """Inputs and all outputs use scaled-return units; fixed zero-based bins.

    Cross-episode variation is pooled *within exact timesteps*, never across
    an entire bin. Population variance uses only timesteps with >=2 episodes,
    weighted by their number of surviving observations. Late survivors are
    conditional samples, not observations of terminated episodes.
    """
    if not predictions_by_episode or len(predictions_by_episode) != len(targets_by_episode):
        raise ValueError('Matching nonempty episodes required')
    if not math.isfinite(constant):
        raise ValueError('Constant must be finite')
    for predictions, targets in zip(predictions_by_episode, targets_by_episode):
        if not targets or len(predictions) != len(targets):
            raise ValueError('Matching nonempty predictions and targets required per episode')
        if any(not math.isfinite(value) for value in (*predictions, *targets)):
            raise ValueError('Predictions and targets must be finite')
    result = []
    for start, end in BINS:
        values = [(pred[t], target[t]) for pred, target in zip(predictions_by_episode, targets_by_episode)
                  for t in range(start, min(len(target), end if end is not None else len(target)))]
        row = {'range': f'{start}–{end-1}' if end is not None else f'{start}+', 'count': len(values)}
        if not values:
            row.update({key: None for key in ('bias', 'mae', 'mse', 'mean_true_return', 'mean_predicted_return',
                                              'constant_mse', 'within_timestep_population_std')})
            row.update(variation_observations=0, peerless_observations=0)
            result.append(row)
            continue
        errors = [prediction-target for prediction, target in values]
        count = len(values)
        row.update(bias=sum(errors)/count, mae=sum(abs(error) for error in errors)/count,
                   mse=sum(error*error for error in errors)/count,
                   mean_true_return=sum(target for _, target in values)/count,
                   mean_predicted_return=sum(prediction for prediction, _ in values)/count,
                   constant_mse=sum((constant-target)**2 for _, target in values)/count)
        squared_deviation, supported_count, peerless = 0., 0, 0
        for t in range(start, min(max(map(len, targets_by_episode)), end or max(map(len, targets_by_episode)))):
            returns = [episode[t] for episode in targets_by_episode if t < len(episode)]
            if len(returns) < 2:
                peerless += len(returns)
                continue
            mean = sum(returns)/len(returns)
            squared_deviation += sum((value-mean)**2 for value in returns)
            supported_count += len(returns)
        row.update(within_timestep_population_std=math.sqrt(squared_deviation/supported_count) if supported_count else None,
                   variation_observations=supported_count, peerless_observations=peerless)
        result.append(row)
    return result


def analyze_saved_critic(checkpoint_path, reference_path, progress=None):
    contents = Path(checkpoint_path).read_bytes()
    import io
    saved = torch.load(io.BytesIO(contents), map_location='cpu', weights_only=True)
    reference = json.loads(Path(reference_path).read_text())
    experiment = saved['experiment']
    for key in ('actor_seed', 'critic_seed', 'training_exploration_seed', 'validation_exploration_seed',
                'training_episodes', 'validation_episodes', 'measurements', 'constant_scaled_prediction',
                'constant_validation_mse', 'physics_extension_sha256', 'torch_version'):
        if experiment.get(key) != reference.get(key):
            raise ValueError(f'Saved critic does not match Step 110: {key}')
    if (saved.get('kind') != 'aura_value' or saved.get('return_scale') != CRITIC_RETURN_SCALE or
        saved.get('discount_factor') != DISCOUNT_FACTOR or saved.get('observation_schema_version') != OBSERVATION_SCHEMA_VERSION or
        experiment['action_schema'] != ACTION_SCHEMA_VERSION or experiment['actor_seed'] != 42):
        raise ValueError('Unsupported critic schema, units or frozen actor configuration')
    physics_hash = hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest()
    if physics_hash != reference['physics_extension_sha256'] or str(torch.__version__) != reference['torch_version']:
        raise ValueError('Physics or PyTorch changed; cannot claim a Step 110 validation replay')
    env = AuraEnv()
    with torch.random.fork_rng(devices=[]):
        torch.manual_seed(reference['actor_seed'])
        actor = AuraPolicy(env.observation_size)
        critic = AuraValue(env.observation_size)
    critic.load_state_dict(saved['model'], strict=True)
    actor.eval().requires_grad_(False)
    critic.eval().requires_grad_(False)
    snapshots = [[p.detach().clone() for p in model.parameters()] for model in (actor, critic)]
    data = collect_critic_rollouts(env, actor, reference['validation_episodes'],
                                   reference['validation_exploration_seed'], progress=progress)
    states, targets = prepare_critic_data(critic, data['states_by_episode'], data['returns_by_episode'])
    with torch.no_grad():
        predictions = critic(states)
    if predictions.shape != targets.shape or not torch.isfinite(predictions).all():
        raise ValueError('Invalid critic predictions')
    mse = torch.nn.functional.mse_loss(predictions, targets).item()
    constant = reference['constant_scaled_prediction']
    constant_mse = torch.nn.functional.mse_loss(torch.full_like(targets, constant), targets).item()
    expected_mse = reference['measurements'][-1]['validation_mse']
    if (data['episode_scores'] != reference['validation_survival_steps'] or len(states) != reference['validation_timesteps'] or
        not math.isclose(mse, expected_mse, rel_tol=1e-6, abs_tol=1e-8) or
        not math.isclose(constant_mse, reference['constant_validation_mse'], rel_tol=1e-6, abs_tol=1e-8)):
        raise ValueError('Validation replay does not reproduce the recorded Step 110 results')
    predicted_episodes, target_episodes = [], []
    offset = 0
    for episode in data['states_by_episode']:
        count = len(episode)
        predicted_episodes.append(predictions[offset:offset+count, 0].tolist())
        target_episodes.append(targets[offset:offset+count, 0].tolist())
        offset += count
    changes = [max((p-old).abs().max().item() for p, old in zip(model.parameters(), before))
               for model, before in zip((actor, critic), snapshots)]
    if any(changes) or any(p.grad is not None for model in (actor, critic) for p in model.parameters()):
        raise RuntimeError('Read-only diagnostic modified a network or accumulated gradients')
    report = {'source_checkpoint': str(checkpoint_path), 'checkpoint_sha256': hashlib.sha256(contents).hexdigest(),
              'reference_report': str(reference_path), 'trajectory_source': 'Deterministic replay of original validation seed; tensors were not persisted in Step 110',
              'replay_survival_steps': data['episode_scores'], 'validation_timesteps': len(states),
              'validation_mse': mse, 'constant_validation_mse': constant_mse, 'constant_scaled_prediction': constant,
              'validation_exploration_seed': reference['validation_exploration_seed'], 'actor_optimizer_updates': 0,
              'critic_optimizer_updates': 0, 'actor_max_parameter_change': changes[0], 'critic_max_parameter_change': changes[1],
              'units': 'Returns divided by 100; MSE in squared scaled-return units',
              'variation_definition': 'sqrt(sum within-exact-timestep squared deviations / supported observation count); population std, timesteps with at least two surviving episodes only',
              'timestep_convention': 'Zero-based pre-action state at t/120 seconds; 300+ includes late survivors only',
              'limitations': 'Replay metrics match saved results, but original state tensors were not archived. Temporal bias does not by itself identify contributions to the policy gradient.',
              'bins': temporal_metrics(predicted_episodes, target_episodes, constant)}
    return report, data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint', type=Path, required=True)
    parser.add_argument('--reference', type=Path, default=Path('docs/critic-step110.json'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--dataset-output', type=Path)
    args = parser.parse_args()
    if any(path and path.exists() for path in (args.output, args.dataset_output)):
        parser.error('Choose new artifact names; existing files are preserved')
    report, data = analyze_saved_critic(args.checkpoint, args.reference,
        progress=lambda episode, total, steps: print(f'Validation replay {episode}/{total}: {steps} steps', flush=True))
    if args.dataset_output:
        with args.dataset_output.open('xb') as output:
            torch.save(data, output)
        report['archived_validation_dataset'] = str(args.dataset_output)
    with args.output.open('x') as output:
        json.dump(report, output, indent=2, allow_nan=False)
        output.write('\n')
    print(json.dumps(report['bins'], indent=2))


if __name__ == '__main__':
    main()
