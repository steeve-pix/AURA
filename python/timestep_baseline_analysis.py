"""Step 113: compare a training-only timestep lookup with the saved value critic.

Validation is loaded from the Step 112 archive. Missing training data can only be
recovered with explicit --replay-training, using the original Step 110 seed and
strict replay checks. This diagnostic has no optimizer and performs no updates.
"""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path

import torch
import aura_python

from .aura_env import AuraEnv, OBSERVATION_SCHEMA_VERSION
from .aura_policy import AuraPolicy, AuraValue, ACTION_SCHEMA_VERSION
from .critic_error_analysis import BINS
from .train_nn import CRITIC_RETURN_SCALE, DISCOUNT_FACTOR, collect_critic_rollouts, prepare_critic_data


def fit_timestep_baseline(training):
    """Fit a timestep-weighted constant and lookup using training returns only."""
    if not training or any(not episode for episode in training):
        raise ValueError('Training requires nonempty episodes')
    if any(not math.isfinite(value) for episode in training for value in episode):
        raise ValueError('Training returns must be finite')
    flat_training = [value for episode in training for value in episode]
    constant = math.fsum(flat_training)/len(flat_training)
    table, counts = [], []
    for t in range(max(map(len, training))):
        peers = [episode[t] for episode in training if t < len(episode)]
        counts.append(len(peers))
        table.append(math.fsum(peers)/len(peers))
    return constant, table, counts


def compare_baselines(training, validation, predictions):
    """All inputs use scaled returns. Fit the constant and lookup to training only."""
    for name, episodes in [('training', training), ('validation', validation), ('predictions', predictions)]:
        if not episodes or any(not episode for episode in episodes):
            raise ValueError(f'{name} requires nonempty episodes')
        if any(not math.isfinite(value) for episode in episodes for value in episode):
            raise ValueError(f'{name} must be finite')
    if len(validation) != len(predictions) or any(len(a) != len(b) for a, b in zip(validation, predictions)):
        raise ValueError('Validation predictions and returns must be aligned')
    constant, table, counts = fit_timestep_baseline(training)
    samples = [(t, target, pred[t], table[t] if t < len(table) else constant, t < len(table))
               for episode, pred in zip(validation, predictions) for t, target in enumerate(episode)]

    def metrics(values):
        n = len(values)
        covered = [row for row in values if row[4]]
        return {'count': n, 'covered_count': len(covered), 'uncovered_count': n-len(covered),
                'constant_mse': math.fsum((row[1]-constant)**2 for row in values)/n if n else None,
                'critic_mse': math.fsum((row[1]-row[2])**2 for row in values)/n if n else None,
                'timestep_mse_with_fallback': math.fsum((row[1]-row[3])**2 for row in values)/n if n else None,
                'timestep_mse_covered_only': math.fsum((row[1]-row[3])**2 for row in covered)/len(covered) if covered else None,
                'covered_training_peer_count_min': min(counts[row[0]] for row in covered) if covered else None}
    bins = []
    for start, end in BINS:
        values = [row for row in samples if row[0] >= start and (end is None or row[0] < end)]
        bins.append({'range': f'{start}–{end-1}' if end is not None else f'{start}+', **metrics(values)})
    return {'constant_scaled_prediction': constant, 'overall': metrics(samples), 'bins': bins,
            'timestep_scaled_predictions': table, 'training_observations_per_timestep': counts,
            'uncovered_timesteps': sorted({row[0] for row in samples if not row[4]}),
            'fallback': 'Training-timestep-weighted constant mean, used only beyond the training lookup range'}


def run_comparison(checkpoint, reference_path, validation_path, training_path=None, replay=False):
    if (training_path is None) == (not replay):
        raise ValueError('Choose a training archive or explicitly authorize original-seed replay')
    contents = Path(checkpoint).read_bytes()
    reference = json.loads(Path(reference_path).read_text())
    saved = torch.load(io.BytesIO(contents), map_location='cpu', weights_only=True)
    experiment = saved['experiment']
    for key, value in reference.items():
        if experiment.get(key) != value:
            raise ValueError(f'Saved critic does not match Step 110: {key}')
    if (saved['kind'] != 'aura_value' or saved['return_scale'] != CRITIC_RETURN_SCALE or
        saved['discount_factor'] != DISCOUNT_FACTOR or saved['observation_schema_version'] != OBSERVATION_SCHEMA_VERSION or
        reference['action_schema'] != ACTION_SCHEMA_VERSION or reference['actor_seed'] != 42 or reference.get('actor_checkpoint_sha256')):
        raise ValueError('Unsupported schema, units or actor source')
    if (hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest() != reference['physics_extension_sha256'] or
        str(torch.__version__) != reference['torch_version']):
        raise ValueError('Physics or PyTorch changed since Step 110')
    env = AuraEnv()
    with torch.random.fork_rng(devices=[]):
        torch.manual_seed(reference['actor_seed'])
        actor = AuraPolicy(env.observation_size)
        critic = AuraValue(env.observation_size)
    critic.load_state_dict(saved['model'], strict=True)
    for model in (actor, critic):
        model.eval().requires_grad_(False)
    snapshots = [[p.detach().clone() for p in model.parameters()] for model in (actor, critic)]
    validation_bytes = Path(validation_path).read_bytes()
    validation = torch.load(io.BytesIO(validation_bytes), map_location='cpu', weights_only=True)
    training = (collect_critic_rollouts(env, actor, reference['training_episodes'], reference['training_exploration_seed'],
        progress=lambda i, n, steps: print(f'Training replay {i}/{n}: {steps} steps', flush=True)) if replay else
        torch.load(training_path, map_location='cpu', weights_only=True))
    tensors, verification = {}, {}
    for label, data in [('training', training), ('validation', validation)]:
        states, targets = prepare_critic_data(critic, data['states_by_episode'], data['returns_by_episode'])
        if (len(data['states_by_episode']) != reference[f'{label}_episodes'] or
            data['episode_scores'] != reference[f'{label}_survival_steps'] or
            [len(ep) for ep in data['states_by_episode']] != data['episode_scores'] or
            len(states) != reference[f'{label}_timesteps'] or states.shape[1] != reference['observation_size']):
            raise ValueError(f'{label} does not match Step 110 episode counts/lengths')
        with torch.no_grad():
            predictions = critic(states)
        if predictions.shape != targets.shape or not torch.isfinite(predictions).all():
            raise ValueError('Invalid critic predictions')
        mse = torch.nn.functional.mse_loss(predictions, targets).item()
        if not math.isclose(mse, reference['measurements'][-1][f'{label}_mse'], rel_tol=1e-6, abs_tol=1e-8):
            raise ValueError(f'{label} critic MSE does not reproduce Step 110')
        expected_range = reference[f'{label}_raw_return_range']
        actual_range = [targets.min().item()*CRITIC_RETURN_SCALE, targets.max().item()*CRITIC_RETURN_SCALE]
        if any(not math.isclose(a,b,rel_tol=1e-6,abs_tol=1e-8) for a,b in zip(actual_range,expected_range)):
            raise ValueError(f'{label} target ranges do not reproduce Step 110')
        verification[label] = {'episodes': len(data['states_by_episode']), 'timesteps': len(states),
                               'critic_mse': mse, 'raw_return_range': actual_range,
                               'survival_steps_match': True, 'reference_metrics_match': True}
        lengths = [len(ep) for ep in data['states_by_episode']]
        tensors[label] = (list(targets[:,0].split(lengths)), list(predictions[:,0].split(lengths)))
    result = compare_baselines([ep.tolist() for ep in tensors['training'][0]],
                              [ep.tolist() for ep in tensors['validation'][0]],
                              [ep.tolist() for ep in tensors['validation'][1]])
    for actual, expected in [(result['constant_scaled_prediction'],reference['constant_scaled_prediction']),
                             (result['overall']['constant_mse'],reference['constant_validation_mse'])]:
        if not math.isclose(actual, expected, rel_tol=1e-6, abs_tol=1e-8):
            raise ValueError('Training constant does not reproduce Step 110')
    changes = [max((p-old).abs().max().item() for p,old in zip(model.parameters(),before))
               for model,before in zip((actor,critic),snapshots)]
    if any(changes) or any(p.grad is not None for model in (actor,critic) for p in model.parameters()):
        raise RuntimeError('Diagnostic modified a network or accumulated gradients')
    if Path(checkpoint).read_bytes() != contents or Path(validation_path).read_bytes() != validation_bytes:
        raise RuntimeError('Source artifacts changed during analysis')
    result['verification'] = verification
    result['overall']['fallback_percent'] = 100*result['overall']['uncovered_count']/result['overall']['count']
    result.update(source_checkpoint=str(checkpoint), checkpoint_sha256=hashlib.sha256(contents).hexdigest(),
                  validation_dataset=str(validation_path), validation_sha256=hashlib.sha256(validation_bytes).hexdigest(),
                  reference_report=str(reference_path), training_source='Authorized deterministic replay of original seed' if replay else str(training_path),
                  training_exploration_seed=reference['training_exploration_seed'], validation_exploration_seed=reference['validation_exploration_seed'],
                  training_episodes=reference['training_episodes'], validation_episodes=reference['validation_episodes'],
                  training_replay_survival_steps=training['episode_scores'], actor_optimizer_updates=0, critic_optimizer_updates=0,
                  actor_max_parameter_change=changes[0], critic_max_parameter_change=changes[1],
                  units='Discounted returns / 100; MSE in squared scaled-return units',
                  limitations='Fixed starting conditions and one frozen policy; lookup tail uses few surviving training episodes. Prediction MSE does not establish policy-gradient quality or a causal explanation for it.')
    return result, training


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint',type=Path,default=Path('models/critics/critic-d8moahss/critic.pt'))
    parser.add_argument('--reference',type=Path,default=Path('docs/critic-step110.json'))
    parser.add_argument('--validation',type=Path,default=Path('models/critics/step112-validation.pt'))
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--training',type=Path)
    source.add_argument('--replay-training',action='store_true')
    parser.add_argument('--training-output',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    if args.replay_training and not args.training_output:
        parser.error('--replay-training requires --training-output to preserve the recovered dataset')
    for path in (args.output,args.training_output):
        if path and (path.exists() or path.resolve() in {p.resolve() for p in (args.checkpoint,args.reference,args.validation,args.training) if p}):
            parser.error('Choose unused output paths; source and historical artifacts are preserved')
    report, training = run_comparison(args.checkpoint,args.reference,args.validation,args.training,args.replay_training)
    if args.training_output:
        with args.training_output.open('xb') as output: torch.save(training,output)
        report['archived_training_dataset']=str(args.training_output)
        report['training_dataset_sha256']=hashlib.sha256(args.training_output.read_bytes()).hexdigest()
    with args.output.open('x') as output:
        json.dump(report,output,indent=2,allow_nan=False); output.write('\n')
    print(json.dumps({'overall':report['overall'],'bins':report['bins']},indent=2))


if __name__=='__main__': main()
