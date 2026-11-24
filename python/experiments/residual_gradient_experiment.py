"""Step 116: paired gradients from a fixed timestep lookup and residual critic.

No optimizers, fitting, or parameter updates. Archive original latent Gaussian
samples so clipping and positive-only motor transforms do not change the score
function when the two gradients are reconstructed.
"""
import argparse
import aura_python
import hashlib
import itertools
import json
import math
import torch
from pathlib import Path

from python.aura_env import LegacyAuraEnv as AuraEnv, OBSERVATION_SCHEMA_VERSION, OBSERVATION_SIZE
from python.aura_policy import AuraPolicy, AuraValue, ACTION_SCHEMA_VERSION, ACTION_TRANSFORMS, outputs_to_action
from python.experiments.residual_critic_experiment import residual_dataset
from python.experiments.timestep_baseline_analysis import fit_timestep_baseline
from python.train_nn import CRITIC_RETURN_SCALE, DISCOUNT_FACTOR, discounted_returns, prepare_critic_data

SEEDS = (11601, 11602, 11603, 11604, 11605)
METHODS = ('timestep', 'residual')


def collect_gradient_rollouts(env, actor, episodes, seed, progress=None):
    """Match AuraPolicy.sample_action while retaining its unclipped latent sample."""
    if type(episodes) is not int or episodes < 1: raise ValueError('Positive episode count required')
    if next(actor.parameters()).device.type != 'cpu': raise ValueError('Controlled rollouts require a CPU actor')
    data = {key: [] for key in ('states_by_episode', 'latent_actions_by_episode', 'actions_by_episode',
                                'rewards_by_episode', 'returns_by_episode', 'episode_scores')}
    was_training = actor.training
    actor.eval()
    try:
        with torch.random.fork_rng(devices=[]), torch.no_grad():
            torch.manual_seed(seed)
            for episode in range(episodes):
                state, _ = env.reset()
                states, latents, actions, rewards = [], [], [], []
                while True:
                    states.append(state.detach().clone())
                    mean = actor(state)
                    sample = torch.distributions.Normal(mean, actor.exploration_std).sample()
                    action = outputs_to_action(sample.clamp(*actor.sample_bounds).detach(), actor.action_transforms)
                    latents.append(sample.detach().clone())
                    actions.append(action)
                    state, reward, terminated, truncated, info = env.step(action)
                    rewards.append(reward)
                    if terminated or truncated: break
                returns = discounted_returns(rewards)
                for key, value in (('states_by_episode', states), ('latent_actions_by_episode', latents),
                                   ('actions_by_episode', actions), ('rewards_by_episode', rewards),
                                   ('returns_by_episode', returns), ('episode_scores', info['steps'])):
                    data[key].append(value)
                if progress: progress(episode + 1, episodes, info['steps'])
    finally:
        actor.train(was_training)
    return data


def paired_policy_gradients(actor, critic, data, table, constant, episode_limit):
    """Return loss gradients without accumulating .grad or stepping an optimizer."""
    if not 0 < episode_limit < float('inf'): raise ValueError('Positive finite actor advantage scale required')
    if len(data['latent_actions_by_episode']) != len(data['states_by_episode']) or any(
            len(actions) != len(states) for actions, states in
            zip(data['latent_actions_by_episode'], data['states_by_episode'])):
        raise ValueError('Each state must have its original latent action')
    states, targets, base, _, lengths = residual_dataset(critic, data, table, constant)
    parameter = next(actor.parameters())
    try:
        latents = torch.stack([value for episode in data['latent_actions_by_episode'] for value in episode]).to(
            parameter)
    except (RuntimeError, TypeError) as error:
        raise ValueError('Consistent latent action tensors required') from error
    if latents.shape != (len(states), len(actor.action_transforms)) or not torch.isfinite(latents).all():
        raise ValueError('Finite [N, action_count] latent actions required')
    with torch.no_grad():
        corrections = critic(states)
        if corrections.shape != targets.shape or not torch.isfinite(corrections).all():
            raise ValueError('Critic must return finite [N, 1] corrections')
        # Both methods use production REINFORCE's episode_limit scale, rather
        # than accidentally making one method 12 times larger at limit 1200.
        advantages = {'timestep': ((targets - base) * CRITIC_RETURN_SCALE / episode_limit).flatten(),
                      'residual': ((targets - (base + corrections)) * CRITIC_RETURN_SCALE / episode_limit).flatten()}
    if any(not torch.isfinite(value).all() for value in advantages.values()): raise ValueError('Nonfinite advantages')
    parameters = tuple(actor.parameters())
    requires_grad = [p.requires_grad for p in parameters]
    was_training = actor.training
    actor.eval()
    vectors, losses = {}, {}
    try:
        # Frozen means no learning update. Autograd still needs these derivatives;
        # grad() returns them directly, leaving every parameter.grad unchanged.
        for p in parameters: p.requires_grad_(True)
        with torch.enable_grad():
            distribution = torch.distributions.Normal(actor(states.to(parameter)), actor.exploration_std)
            log_probs = distribution.log_prob(latents.detach()).sum(dim=1)
            for index, method in enumerate(METHODS):
                loss = -(log_probs * advantages[method].to(log_probs)).sum() / len(lengths)
                gradient = torch.autograd.grad(loss, parameters, retain_graph=index == 0)
                vector = torch.cat([value.detach().flatten() for value in gradient]).cpu()
                if not torch.isfinite(vector).all() or not torch.isfinite(loss): raise RuntimeError(
                    'Nonfinite policy gradient')
                vectors[method] = vector
                losses[method] = loss.item()
    finally:
        for p, enabled in zip(parameters, requires_grad): p.requires_grad_(enabled)
        actor.train(was_training)
    fallback = sum(max(0, length - len(table)) for length in lengths)
    return vectors, {'losses': losses, 'timesteps': len(states), 'fallback_count': fallback,
                     'clipped_latent_coordinates': int(
                         ((latents < actor.sample_bounds[0]) | (latents > actor.sample_bounds[1])).sum()),
                     'mean_absolute_advantage': {method: value.abs().mean().item() for method, value in
                                                 advantages.items()}}


def parameter_layout(actor):
    result = []
    offset = 0
    for name, p in actor.named_parameters():
        result.append(
            {'name': name, 'shape': list(p.shape), 'count': p.numel(), 'start': offset, 'end': offset + p.numel()})
        offset += p.numel()
    return result


def summarize_gradients(gradients, layout):
    """Unbiased sample variance across batch estimates, for every scalar weight."""
    stacks = {method: torch.stack(gradients[method]).double() for method in METHODS}
    if stacks['timestep'].shape != stacks['residual'].shape or len(stacks['timestep']) < 2:
        raise ValueError('Matching gradients from at least two batches required')
    if any(not torch.isfinite(value).all() for value in stacks.values()): raise ValueError('Finite gradients required')
    if not layout or layout[-1]['end'] != stacks['timestep'].shape[1]: raise ValueError(
        'Gradient parameter layout mismatch')
    labels = [chr(ord('A') + i) for i in range(len(stacks['timestep']))]
    summary, variance = {}, {}
    for method, stack in stacks.items():
        variance[method] = stack.var(dim=0, unbiased=True)
        norms = stack.norm(dim=1)
        pairs = {}
        for i, j in itertools.combinations(range(len(stack)), 2):
            pairs[f'{labels[i]}–{labels[j]}'] = (torch.dot(stack[i], stack[j]) / (norms[i] * norms[j])).clamp(-1,
                                                                                                              1).item() if \
            norms[i] > 0 and norms[j] > 0 else None
        defined = [value for value in pairs.values() if value is not None]
        summary[method] = {'gradient_norms': norms.tolist(), 'mean_gradient_norm': norms.mean().item(),
                           'mean_gradient_vector_norm': stack.mean(dim=0).norm().item(),
                           'pairwise_cosines': pairs,
                           'mean_pairwise_cosine': math.fsum(defined) / len(defined) if defined else None,
                           'total_gradient_variance': variance[method].sum().item(),
                           'mean_coordinate_variance': variance[method].mean().item()}
    original = summary['timestep']['total_gradient_variance']
    combined = summary['residual']['total_gradient_variance']
    by_tensor = []
    for spec in layout:
        row = {**spec}
        for method in METHODS:
            values = variance[method][spec['start']:spec['end']]
            row[method + '_variance_sum'] = values.sum().item()
            row[method + '_mean_coordinate_variance'] = values.mean().item()
        by_tensor.append(row)
    delta = variance['residual'] - variance['timestep']
    return {'methods': summary,
            'variance_reduction_percent': 100 * (original - combined) / original if original else None,
            'per_parameter_tensor': by_tensor, 'coordinate_variance_comparison': {
            'lower': int((delta < 0).sum()), 'equal': int((delta == 0).sum()), 'higher': int((delta > 0).sum())},
            'variance_definition': 'Unbiased per-coordinate sample variance across batch gradients (ddof=1); total is covariance trace',
            'cosine_definition': 'All pairwise cosines within each method; zero-norm pairs are undefined'}, variance


def run_residual_gradient_experiment(actor, critic, table, constant, *, env_factory=AuraEnv, batch_size=32,
                                     exploration_seeds=SEEDS, progress=None, on_batch=None):
    if len(exploration_seeds) != 5 or len(set(exploration_seeds)) != 5 or set(exploration_seeds) & {11501, 11502}:
        raise ValueError('Five independent streams, separate from Step 115 training/test, required')
    if type(batch_size) is not int or batch_size < 2: raise ValueError('At least two whole episodes per batch required')
    for model in (actor, critic): model.eval().requires_grad_(False)
    before = [[p.detach().clone() for p in model.parameters()] for model in (actor, critic)]
    if any(p.grad is not None for model in (actor, critic) for p in model.parameters()): raise ValueError(
        'Networks must start without accumulated gradients')
    gradients = {method: [] for method in METHODS}
    rows = []
    limit = None
    for label, seed in zip('ABCDE', exploration_seeds):
        env = env_factory()
        if limit is not None and limit != env.episode_limit: raise ValueError(
            'All batches must share the same actor scale')
        limit = env.episode_limit
        callback = (
            lambda episode, total, steps, label=label: progress(label, episode, total, steps)) if progress else None
        data = collect_gradient_rollouts(env, actor, batch_size, seed, callback)
        vectors, details = paired_policy_gradients(actor, critic, data, table, constant, limit)
        for method in METHODS: gradients[method].append(vectors[method].double())
        row = {'batch': label, 'exploration_seed': seed, 'episodes': batch_size,
               'survival_steps': data['episode_scores'], **details,
               'gradient_norms': {method: vectors[method].double().norm().item() for method in METHODS}}
        if on_batch: on_batch(label, data, vectors)
        rows.append(row)
        if progress: progress(label, batch_size, batch_size, -1)
    changes = [max((p.detach() - old).abs().max().item() for p, old in zip(model.parameters(), snapshot))
               for model, snapshot in zip((actor, critic), before)]
    if any(changes) or any(p.grad is not None for model in (actor, critic) for p in model.parameters()):
        raise RuntimeError('Diagnostic modified a network or accumulated .grad')
    layout = parameter_layout(actor)
    summary, variance = summarize_gradients(gradients, layout)
    report = {'batches': rows, **summary, 'actor_max_parameter_change': changes[0],
              'critic_max_parameter_change': changes[1],
              'actor_optimizer_updates': 0, 'critic_optimizer_updates': 0, 'actor_has_no_gradients': True,
              'critic_has_no_gradients': True,
              'same_trajectories_for_both_baselines': True, 'baseline_fitted_to_evaluation_batches': False,
              'exploration_seeds': list(exploration_seeds), 'episodes_per_batch': batch_size, 'independent_batches': 5,
              'actor_advantage_denominator': limit,
              'scaled_return_to_actor_advantage_factor': CRITIC_RETURN_SCALE / limit,
              'advantage_formulas': {'timestep': '(G/100 - b_t) * 100/episode_limit',
                                     'residual': '(G/100 - (b_t + V(s))) * 100/episode_limit'},
              'loss_reduction': 'Negative sum of log_probability * detached advantage over timesteps / episode count',
              'production_reinforce_changed': False,
              'total_environment_steps': sum(row['timesteps'] for row in rows),
              'total_fallback_count': sum(row['fallback_count'] for row in rows),
              'limitations': 'Five batch estimates only. Ten cosine pairs and thousands of parameter coordinates are correlated, not independent repetitions. Variance and agreement do not establish improved learning or balance.'}
    return report, {'kind': 'gradient_diagnostic', 'gradients': gradients, 'coordinate_variances': variance,
                    'parameter_layout': layout}


def load_reference(directory):
    """Refuse a changed actor, critic, baseline, action contract or physics build."""
    directory = Path(directory)
    report_path = directory / 'report.json'
    reference = json.loads(report_path.read_text())
    for name in ('critic.pt', 'frozen-actor.pt', 'training.pt', 'test.pt'):
        path = directory / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != reference['artifacts'][name]['sha256']:
            raise ValueError(f'Step 115 artifact changed: {name}')
    if (reference['action_count'] != 32 or reference['observation_size'] != OBSERVATION_SIZE or
            reference['action_schema'] != ACTION_SCHEMA_VERSION or reference[
                'observation_schema'] != OBSERVATION_SCHEMA_VERSION or
            reference['physical_action_schema'] != aura_python.action_schema_version or reference[
                'return_scale'] != CRITIC_RETURN_SCALE or
            reference['discount_factor'] != DISCOUNT_FACTOR or reference['critic_updates'] != 20 or reference[
                'actor_seed'] != 42 or
            reference['torch_version'] != str(torch.__version__)):
        raise ValueError('Current actor/critic configuration does not match Step 115')
    if hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest() != reference['physics_extension_sha256']:
        raise ValueError('Current physics extension differs from Step 115')
    root = Path(__file__).resolve().parents[2]
    for name, digest in reference['provenance']['source_sha256'].items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest: raise ValueError(
            f'Step 115 source changed: {name}')
    saved = torch.load(directory / 'frozen-actor.pt', map_location='cpu', weights_only=True)
    value = torch.load(directory / 'critic.pt', map_location='cpu', weights_only=True)
    for checkpoint in (saved, value):
        if (checkpoint['observation_size'] != OBSERVATION_SIZE or checkpoint[
            'observation_schema_version'] != OBSERVATION_SCHEMA_VERSION or
                checkpoint['action_schema_version'] != ACTION_SCHEMA_VERSION or checkpoint[
                    'physical_action_schema_version'] != aura_python.action_schema_version):
            raise ValueError('Actor and critic must share the current observation/action contracts')
    if (saved['kind'] != 'frozen_actor_snapshot' or saved['seed'] != 42 or saved[
        'action_transforms'] != ACTION_TRANSFORMS or
            value['kind'] != 'aura_value_residual' or value[
                'physical_action_schema_version'] != aura_python.action_schema_version):
        raise ValueError('Saved actor transforms or critic contract mismatch')
    with torch.random.fork_rng(devices=[]):
        actor = AuraPolicy(OBSERVATION_SIZE)
        critic = AuraValue(OBSERVATION_SIZE)
    actor.load_state_dict(saved['model'], strict=True)
    critic.load_state_dict(value['model'], strict=True)
    actor.eval().requires_grad_(False)
    critic.eval().requires_grad_(False)
    training = torch.load(directory / 'training.pt', map_location='cpu', weights_only=True)
    _, targets = prepare_critic_data(critic, training['states_by_episode'], training['returns_by_episode'])
    lengths = [len(ep) for ep in training['states_by_episode']]
    constant, table, counts = fit_timestep_baseline([ep[:, 0].tolist() for ep in targets.split(lengths)])
    baseline = value['baseline']
    if (table != baseline['table'] or constant != baseline['constant'] or counts != baseline['training_counts'] or
            table != reference['timestep_scaled_predictions'] or constant != reference['constant_scaled_prediction']):
        raise ValueError('Saved timestep lookup does not match the separate training archive')
    return actor, critic, table, constant, reference


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-dir', type=Path, default=Path('models/critics/step115-current-physics'))
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.exists(): parser.error('Choose an unused directory; earlier experiments are preserved')
    actor, critic, table, constant, reference = load_reference(args.reference_dir)
    protected = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in args.reference_dir.iterdir() if
                 path.is_file()}
    config = {'experiment': 'step116-paired-residual-gradients', 'exploration_seeds': list(SEEDS),
              'episodes_per_batch': 32,
              'independent_batches': 5, 'actor_optimizer_updates': 0, 'critic_optimizer_updates': 0,
              'observation_size': OBSERVATION_SIZE, 'action_count': len(actor.action_transforms),
              'action_schema': ACTION_SCHEMA_VERSION, 'physical_action_schema': aura_python.action_schema_version,
              'observation_schema': OBSERVATION_SCHEMA_VERSION, 'action_keys': list(actor.action_transforms),
              'timestep_baseline_coverage': len(table), 'timestep_fallback_constant': constant,
              'actor_exploration_std': actor.exploration_std, 'latent_sample_bounds': list(actor.sample_bounds),
              'step115_artifacts_sha256': protected, 'physics_extension_sha256': reference['physics_extension_sha256'],
              'diagnostic_source_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    args.output_dir.mkdir(parents=True, exist_ok=False)
    (args.output_dir / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
    artifacts = {}

    def save(name, data):
        path = args.output_dir / name
        with path.open('xb') as stream: torch.save(data, stream)
        artifacts[name] = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}

    def on_batch(label, data, vectors):
        save(f'batch-{label}.pt', {'kind': 'gradient_rollouts', 'data': data, 'gradients': vectors})

    def progress(label, episode, total, steps):
        if steps == -1:
            print(f'Batch {label}: both gradients calculated', flush=True)
        elif episode % 8 == 0:
            print(f'Batch {label}: episode {episode}/{total} / {steps} steps', flush=True)

    report, gradients = run_residual_gradient_experiment(actor, critic, table, constant, progress=progress,
                                                         on_batch=on_batch)
    save('gradients.pt', gradients)
    if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest for path, digest in protected.items()):
        raise RuntimeError('Step 115 artifacts changed during the diagnostic')
    # Recheck the source/schema/physics contracts after the simulation too.
    load_reference(args.reference_dir)
    if hashlib.sha256(Path(__file__).read_bytes()).hexdigest() != config['diagnostic_source_sha256']:
        raise RuntimeError('Diagnostic source changed during collection')
    report.update(provenance=config, artifacts=artifacts, step115_artifacts_unchanged=True,
                  action_count=len(actor.action_transforms), observation_size=OBSERVATION_SIZE,
                  action_schema=ACTION_SCHEMA_VERSION, physical_action_schema=aura_python.action_schema_version)
    (args.output_dir / 'experiment.py').write_bytes(Path(__file__).read_bytes())
    with (args.output_dir / 'report.json').open('x') as stream:
        json.dump(report, stream, indent=2, allow_nan=False);stream.write('\n')
    for method, metrics in report['methods'].items():
        print(
            f"{method}: mean cosine {metrics['mean_pairwise_cosine']:+.6f} / variance {metrics['total_gradient_variance']:.9f} / mean norm {metrics['mean_gradient_norm']:.6f}",
            flush=True)
    print(f"Variance reduction: {report['variance_reduction_percent']:+.2f}%", flush=True)
    print(f"Report: {args.output_dir / 'report.json'}", flush=True)


if __name__ == '__main__': main()
