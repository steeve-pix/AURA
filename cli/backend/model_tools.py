"""Read-only model comparisons, health checks and durable training reports.

Snapshots are loaded once. No optimizers are constructed or stepped; checkpoints,
locks and evaluation-cache files are never written by these commands.
"""
import argparse
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import select
import sys
import time
from datetime import datetime, timezone

import torch
from python.model_checkpoint import policy_from_checkpoint
from python.arena_env import checkpoint_environment
from python.aura_policy import AuraPolicy
from python.train_nn import evaluate_balance


class Events:
    def __init__(self, enabled=False, interactive=False):
        self.enabled, self.interactive = enabled, interactive
        self.pending = ''

    def emit(self, kind, *values):
        values = [' '.join(str(v).split()) for v in values]
        if self.enabled:
            print('AURA_RESEARCH', kind, *values, sep='\t', flush=True)
        elif kind in ('row', 'section', 'error', 'saved'):
            print(' / '.join(values), flush=True)

    def service(self):
        if self.interactive and select.select([sys.stdin], [], [], 0)[0]:
            data = os.read(sys.stdin.fileno(), 1024)
            if not data:
                raise KeyboardInterrupt
            self.pending += data.decode('ascii', errors='ignore')
            while '\n' in self.pending:
                line, self.pending = self.pending.split('\n', 1)
                if line == 'quit':
                    raise KeyboardInterrupt


def snapshot(path):
    payload = Path(path).read_bytes()
    data = torch.load(io.BytesIO(payload), map_location='cpu', weights_only=True)
    return data, hashlib.sha256(payload).hexdigest()


def environment(data, events):
    env = checkpoint_environment(data)
    env.runtime_service = events.service
    if not getattr(env, 'is_arena', False):
        step = env.step
        def observed_step(action):
            events.service()
            return step(action)
        env.step = observed_step
    return env


def tensor_health(values):
    tensors = [v for v in values.values() if isinstance(v, torch.Tensor)]
    finite = all(bool(torch.isfinite(v).all()) for v in tensors)
    return dict(parameters=sum(v.numel() for v in tensors), finite=finite,
                norm=math.sqrt(sum(float(v.double().square().sum()) for v in tensors)) if finite else None)


def optimizer_step(optimizer):
    return sorted({int(state.get('step', 0)) for state in optimizer.get('state', {}).values()})


def history_summary(data):
    history = data.get('training_history', [])
    return dict(recorded_updates=sum('actor_loss' in row for row in history),
                first_update=history[0]['update'] if history else None,
                last_update=history[-1]['update'] if history else None,
                saved_updates=data.get('updates_completed', 0))


def compare(path, data, digest, events):
    env = environment(data, events)
    policy_from_checkpoint(data, env)  # Validate before any evaluation.
    seed = data.get('training_seed')
    variants = []
    if 'initial_actor' in data:
        initial = dict(data, model=data['initial_actor'])
        variants.append(('Initial', initial, 'Saved initial actor'))
    elif seed is not None:
        with torch.random.fork_rng():
            torch.manual_seed(seed)
            actor = AuraPolicy(env.observation_size, exploration_std=data['exploration_std'])
        initial = dict(data, model=actor.state_dict())
        # Strict reconstruction: an architecture mismatch is a clear error, not a substitute.
        policy_from_checkpoint(initial, env)
        variants.append(('Seed baseline', initial, f'Reconstructed seed {seed}; original weights were not retained'))
    source = path.with_name(path.stem.removesuffix('_best') + '.pt')
    best = source.with_name(source.stem + '_best.pt')
    snapshots = []
    for label, candidate in [('Best', best), ('Latest', source)]:
        events.service()
        if candidate == path:
            saved, sha = data, digest
        elif candidate.is_file():
            saved, sha = snapshot(candidate)
        else:
            events.emit('row', label, 'Unavailable', candidate.name)
            continue
        if saved.get('training_run_id') != data.get('training_run_id'):
            events.emit('row', label, 'Skipped', 'Different training run')
            continue
        policy_from_checkpoint(saved, env)
        variants.append((label, saved, f'Update {saved.get("updates_completed", 0)} / {candidate.name}'))
        snapshots.append(dict(label=label, path=str(candidate), sha256=sha))
    seeds = [190042, 190043, 190044]
    events.emit('section', 'MATCHED EVALUATION')
    events.emit('row', 'Protocol', 'CPU / deterministic + 3 exploration streams', 'Same saved world, reward and exploration')
    events.emit('row', 'World', f'{env.agents if getattr(env,"is_arena",False) else 1} agents', 'Survival is diagnostic; balanced duration is primary')
    output = []
    total = len(variants) * 4
    for label, saved, provenance in variants:
        policy = policy_from_checkpoint(saved, env)
        policy.eval()
        events.emit('section', label.upper())
        events.emit('row', 'Checkpoint', provenance, '')
        results = []
        for index, stream in enumerate([None, *seeds]):
            events.service()
            events.emit('progress', len(output) * 4 + index, total, f'{label} / {"deterministic" if stream is None else "stream " + str(stream)}')
            with torch.no_grad(), torch.random.fork_rng():
                result = evaluate_balance(env, policy, stochastic=stream is not None, seed=stream if stream is not None else 10042)
            results.append(result)
            events.emit('row', 'Deterministic' if stream is None else f'Stream {stream}',
                        f'{result["balanced_seconds"]:.3f} s balanced / reward {result["total_reward"]:.2f}',
                        f'{result["steps"]} survival steps / {result["termination_reason"]}')
        average = {key: sum(r[key] for r in results[1:]) / 3 for key in
                   ('balanced_seconds', 'balanced_steps', 'total_reward', 'steps', 'fall_rate')}
        events.emit('row', 'Stochastic mean', f'{average["balanced_seconds"]:.3f} s balanced / reward {average["total_reward"]:.2f}',
                    f'Fall rate {average["fall_rate"]:.0%} / 3 matched streams')
        events.emit('comparison', label, results[0]['balanced_seconds'], average['balanced_seconds'],
                    average['total_reward'], average['fall_rate'])
        output.append(dict(label=label, provenance=provenance, deterministic=results[0],
                           stochastic=results[1:], stochastic_mean=average))
    events.emit('progress', total, total, 'Comparison complete')
    events.emit('section', 'INTERPRETATION')
    events.emit('row', 'Evidence', 'Small diagnostic comparison', 'Three streams do not establish generalized balance')
    return dict(seeds=seeds, device='cpu', snapshots=snapshots, results=output)


def diagnose(data, events):
    events.emit('section', 'CHECKPOINT HEALTH')
    checks = {}
    for label, values in [('Actor', data['model']), ('Critic', data.get('critic', {}).get('model', {}))]:
        health = tensor_health(values)
        checks[label.lower()] = health
        events.emit('row', label, 'Finite' if health['finite'] and health['parameters'] else 'Needs attention',
                    f'{health["parameters"]:,} parameters / norm {health["norm"]:.4f}' if health["norm"] is not None else "Non-finite tensor values")
    actor_steps = optimizer_step(data.get('optimizer', {}))
    critic_steps = optimizer_step(data.get('critic', {}).get('optimizer', {}))
    events.emit('row', 'Adam steps', f'Actor {actor_steps} / critic {critic_steps}',
                'Aligned' if actor_steps == critic_steps else 'Different optimizer steps: investigate')
    env = environment(data, events)
    policy = policy_from_checkpoint(data, env)
    events.emit('row', 'Environment', 'Compatible with current Balance System', f'{data["observation_size"]} inputs / {len(data["action_transforms"])} actions')
    summary = history_summary(data)
    events.emit('row', 'History', f'{summary["recorded_updates"]} recorded / {summary["saved_updates"]} saved updates',
                'Earlier unrecorded metrics cannot be reconstructed')
    recorded = [row for row in data.get('training_history', []) if row.get('deterministic')]
    if len(recorded) >= 4:
        window = min(10, len(recorded) // 2)
        early = sum(row['deterministic']['balanced_seconds'] for row in recorded[:window]) / window
        recent = sum(row['deterministic']['balanced_seconds'] for row in recorded[-window:]) / window
        events.emit('row', 'Recorded balance trend', f'{early:.3f} s early / {recent:.3f} s recent',
                    f'Mean of {window} evaluations at each end / matched evaluation protocol only')
    events.emit('section', 'BOUNDED DETERMINISTIC CHECK')
    events.emit('progress', 0, 1, 'Evaluating frozen actor on CPU')
    policy.eval()
    with torch.no_grad():
        result = evaluate_balance(env, policy)
    events.emit('progress', 1, 1, 'Diagnostic complete')
    events.emit('row', 'Controlled balance', f'{result["balanced_seconds"]:.3f} s / {result["balanced_steps"]} steps',
                f'Longest interval {result["longest_balanced_steps"]} steps / {result["recoveries"]} recoveries')
    events.emit('row', 'Quality / return', f'{result["average_balance_quality"]:.4f} / {result["total_reward"]:.2f}',
                f'{result["termination_reason"]} / {result["steps"]} survival steps')
    events.emit('section', 'LIMITS')
    events.emit('row', 'Learning', 'Finite weights do not prove improvement', 'Use /compare for initial, best and latest policies')
    events.emit('row', 'Critic', 'Weight health only', 'Value prediction accuracy requires held-out return targets')
    return dict(health=checks, actor_steps=actor_steps, critic_steps=critic_steps, history=summary, deterministic=result)


def write_report(path, data, digest, analysis, mode, directory=None):
    """Export new files only, never alter a checkpoint or its evaluation cache."""
    directory = Path(directory) if directory is not None else (
        Path(__file__).resolve().parents[2] / 'reports' / mode)
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S-%fZ')
    base = directory / f'{path.stem}-{mode}-{stamp}'
    metadata = {key: data.get(key) for key in ('training_run_id', 'training_seed', 'episodes_completed',
        'updates_completed', 'training_schedule', 'environment_version', 'reward_version', 'termination_version',
        'reward_name', 'termination_mode', 'quality_version', 'floor_friction_version', 'arena_agents',
        'arena_spacing', 'best_balance_metrics', 'observation_size', 'exploration_std', 'layers')}
    metadata.update(actions=len(data.get('action_transforms', {})),
        actor_learning_rate=data.get('optimizer', {}).get('param_groups', [{}])[0].get('lr'),
        critic_learning_rate=data.get('critic', {}).get('optimizer', {}).get('param_groups', [{}])[0].get('lr'),
        critic_architecture=data.get('critic', {}).get('architecture'),
        critic_return_scale=data.get('critic', {}).get('return_scale'),
        environment_contract=data.get('critic', {}).get('environment_contract'))
    payload = dict(report_version=1, generated_utc=stamp, checkpoint=str(path), sha256=digest,
                   metadata=metadata, history_summary=history_summary(data), history=data.get('training_history', []), analysis=analysis)
    with base.with_suffix('.json').open('x') as handle:
        json.dump(payload, handle, indent=2, allow_nan=False)
    evaluation_fields = ('balanced_seconds', 'balanced_steps', 'unbalanced_steps', 'longest_balanced_steps',
                         'recoveries', 'fall_rate', 'average_balance_quality', 'total_reward', 'steps', 'termination_reason')
    columns = ['update', 'episodes_completed', 'actor_loss', 'critic_loss', 'gradient_norm', 'max_parameter_change',
               'mean_survival'] + [label+'_'+field for label in ('deterministic','stochastic') for field in evaluation_fields]
    with base.with_suffix('.csv').open('x', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        for row in payload['history']:
            record = {key: row.get(key, '') for key in columns}
            for label in ('deterministic', 'stochastic'):
                for field in evaluation_fields:
                    record[label+'_'+field] = row.get(label, {}).get(field, '')
            writer.writerow(record)
    summary_lines = []
    if mode == 'compare':
        summary_lines = ['## Policy comparison', '', '| Policy | Deterministic balance (s) | Stochastic balance (s) | Stochastic reward |',
                         '| --- | ---: | ---: | ---: |']
        for row in analysis.get('results', []):
            summary_lines.append(f"| {row['label']} | {row['deterministic']['balanced_seconds']:.3f} | {row['stochastic_mean']['balanced_seconds']:.3f} | {row['stochastic_mean']['total_reward']:.2f} |")
        summary_lines += ['', 'Stochastic means use three matched exploration streams.', '',
            '| Policy | Exploration seed | Balanced seconds | Reward | Survival steps |',
            '| --- | ---: | ---: | ---: | ---: |']
        for row in analysis.get('results', []):
            for seed, result in zip(analysis['seeds'], row['stochastic']):
                summary_lines.append(f"| {row['label']} | {seed} | {result['balanced_seconds']:.3f} | {result['total_reward']:.2f} | {result['steps']} |")
        summary_lines += ['', 'All policies were evaluated on CPU in the same saved environment.',
            'Reconstructed seed baselines are labeled in the JSON evidence; they are not saved original weights.', '']
    elif mode == 'diagnose' and analysis.get('deterministic'):
        d = analysis['deterministic']
        summary_lines = ['## Balance check', '', f"Controlled balance: {d['balanced_seconds']:.3f} seconds.",
                         f"Reward: {d['total_reward']:.2f}. Termination: {d['termination_reason']}.", '']
    if payload['history']:
        summary_lines += ['## Recent recorded updates', '',
            '| Update | Deterministic balanced seconds | Stochastic balanced seconds | Actor loss | Critic loss |',
            '| ---: | ---: | ---: | ---: | ---: |']
        for row in payload['history'][-20:]:
            summary_lines.append(f"| {row['update']} | {row.get('deterministic', {}).get('balanced_seconds', '')} | {row.get('stochastic', {}).get('balanced_seconds', '')} | {row.get('actor_loss', '')} | {row.get('critic_loss', '')} |")
        summary_lines += ['', 'Full recorded history is retained in the JSON and CSV exports.', '']
    environment_lines = ['| Setting | Saved value |', '| --- | --- |']
    for key in ('training_seed','environment_version','reward_name','reward_version','termination_mode',
                'termination_version','quality_version','floor_friction_version','arena_agents','arena_spacing',
                'observation_size','actions','exploration_std','actor_learning_rate','critic_learning_rate','critic_return_scale'):
        environment_lines.append(f"| {key.replace('_', ' ').capitalize()} | {metadata.get(key) if metadata.get(key) is not None else 'Not recorded'} |")
    lines = [f'# AURA {mode} report' , '', f'Model: {path.name}', f'Snapshot SHA-256: {digest}',
             f'Generated (UTC): {stamp}', '', '## Saved training', '',
             f'- Updates: {data.get("updates_completed", 0)}', f'- Episodes: {data.get("episodes_completed", 0)}',
             f'- Recorded update history: {history_summary(data)["recorded_updates"]}',
             '- Missing historical metrics are unavailable; no values have been invented.',
             '- Balanced duration is the primary metric; survival alone does not show balance.', '',
             *summary_lines, '## Environment', '', *environment_lines, '',
             'The checkpoint was read from a frozen snapshot. No training updates were performed.',
             f'Full history and individual results: {base.with_suffix(".json").name}',
             f'Per-update metrics: {base.with_suffix(".csv").name}', '']
    with base.with_suffix('.md').open('x') as handle:
        handle.write('\n'.join(lines))
    return base.with_suffix('.md')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['compare', 'diagnose', 'report'])
    parser.add_argument('checkpoint', type=Path)
    parser.add_argument('--events', action='store_true')
    parser.add_argument('--interactive', action='store_true')
    args = parser.parse_args()
    events = Events(args.events, args.interactive)
    try:
        data, digest = snapshot(args.checkpoint)
        events.emit('start', args.mode, args.checkpoint.name, data.get('updates_completed', 0))
        events.emit('row', 'Snapshot', digest[:16], f'Update {data.get("updates_completed", 0)} / read-only')
        events.emit('row', 'Reward', data.get('reward_name', 'Unknown'), f'Termination: {data.get("termination_mode", "legacy")}')
        if args.mode == 'compare':
            analysis = compare(args.checkpoint, data, digest, events)
        elif args.mode == 'diagnose':
            analysis = diagnose(data, events)
        else:
            analysis = dict(history=history_summary(data), note='Saved measurements only; no evaluation launched')
            events.emit('section', 'TRAINING HISTORY')
            events.emit('row', 'Saved updates', data.get('updates_completed', 0), f'{data.get("episodes_completed",0):,} episodes')
            events.emit('row', 'Recorded updates', analysis['history']['recorded_updates'], 'Missing past measurements remain unavailable')
            if data.get('training_history'):
                for label in ('deterministic', 'stochastic'):
                    last = data['training_history'][-1].get(label, {})
                    events.emit('row', label.capitalize(), f'{last.get("balanced_seconds",0):.3f} s balanced', f'Reward {last.get("total_reward",0):.2f}')
        events.service()
        report = write_report(args.checkpoint, data, digest, analysis, args.mode)
        events.emit('section', 'EXPORTS')
        events.emit('saved', report)
        events.emit('row', 'Files', report.name, f'Markdown / JSON / per-update CSV in {report.parent}')
        events.emit('complete')
        return 0
    except KeyboardInterrupt:
        events.emit('stopped', 'Cancelled / checkpoints unchanged')
        return 130
    except (ValueError, RuntimeError, OSError, KeyError, TypeError) as error:
        events.emit('error', str(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
