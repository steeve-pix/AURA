"""Step 130: diagnostic common-horizon failure utility; no training or physics.

The existing terminal reward is retained. Virtual failure utility begins on the
next transition and is converted analytically to a terminal-return correction.
"""
import argparse
import copy
import csv
import hashlib
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / 'build-release')]
from python.balance_reward_v2 import RewardV2Config
from python.failure_tail import discounted_failure_tail
from python.diagnostics.reward_incentive_audit import read_trace, synthetic_cases, classify, score
from python.train_nn import discounted_returns


def candidate_utilities(trace_dir):
    """Reference levels anchor normative candidates; they are not physical laws."""
    collapse_rewards = []
    for path in sorted(trace_dir.glob('*.csv')):
        if path.name.startswith('fixture-'):
            continue
        with path.open() as file:
            for row in csv.DictReader(file):
                if row['terminated'] == '1':
                    continue
                if row['deep_collapse'] == '1' or row['contact_collapse'] == '1':
                    collapse_rewards.append(float(row['reward_total']) - float(row['reward_recovery'])
                                            - float(row['reward_terminal']))
    if not collapse_rewards:
        raise ValueError('No preterminal collapse measurements found')
    values = sorted(collapse_rewards)
    middle = len(values) // 2
    median = values[middle] if len(values) % 2 else (values[middle - 1] + values[middle]) / 2
    config = RewardV2Config()
    return {
        'zero': dict(value=0., interpretation='Failure has no ongoing utility; negative control.'),
        'stationary_collapse': dict(value=-(config.posture_weight + config.height_weight),
                                    interpretation='Low, tilted, unsupported failure with no excessive motion; posture and height penalty bound.'),
        'observed_collapse': dict(value=median, samples=len(values), observed_min=min(values), observed_max=max(values),
                                  interpretation='Constant task utility anchored to pooled preterminal deep/contact-collapse median, excluding shaping and fall component.'),
        'physical_lower_bound': dict(value=-(config.posture_weight + config.height_weight + config.motion_weight),
                                     interpretation='Existing unshaped nonterminal lower bound; worst physical quality, not an extra tuned coefficient.')}


def load_sequences(trace_dir, classifier):
    native = {path.stem: read_trace(path) for path in sorted(trace_dir.glob('*.csv'))
              if not path.name.startswith('fixture-')}
    fixtures, metadata = synthetic_cases(native['neutral'][0]['neutral_height_references'])
    sequences = classify(fixtures, classifier)
    for name, sequence in native.items():
        key = 'physical_' + name
        fork = 80 if name in ('neutral', 'brief_airborne', 'rapid_rotation', 'recovery_attempt') else 0
        sequences[key] = sequence
        metadata[key] = dict(kind='integrated_physics_replay', fork_step=fork)
    return sequences, metadata


def horizon_scores(sequences, metadata, horizon):
    """Shorter sensitivity caps reuse known measurements, never invent a future.

    Only the diagnostic cap flags change. Reward V2 handles its normal end-
    potential closure. Production time limits and classifier states are untouched.
    """
    result = {}
    for name, sequence in sequences.items():
        fork = metadata[name]['fork_step']
        if fork >= horizon:
            continue
        capped = []
        for observation in sequence:
            obs = copy.deepcopy(observation)
            if obs['steps'] == horizon:
                obs.update(truncated=True, done=True,
                           termination_reason='fallen' if obs['terminated'] else 'time_limit')
            capped.append(obs)
            if obs['done']:
                break
        if not capped[-1]['done']:
            raise ValueError('Sensitivity horizon extends beyond available measurements')
        result[name] = score(capped, fork)
    return result


def adjust_case(row, fork, utility, gamma, horizon):
    original = discounted_returns([r['reward'] for r in row['records']], gamma)[0]
    terminal = row['terminal_step']
    local_tail = discounted_failure_tail(terminal, fallen=row['terminated'],
                                         failure_utility=utility, horizon=horizon, gamma=gamma)
    contribution = gamma ** (terminal - fork - 1) * local_tail
    return dict(original_return=original, terminal_relative_tail=local_tail,
                failure_tail_contribution=contribution, adjusted_return=original + contribution,
                difference=contribution, terminal_step=terminal, terminated=row['terminated'],
                truncated=row['truncated'],
                remaining_virtual_ticks=max(0, horizon - terminal) if row['terminated'] else 0)


def comparisons(results, original_comparisons):
    rows = []
    for pair in original_comparisons:
        if pair['recovery'] not in results or pair['rapid_fall'] not in results:
            continue
        delta = results[pair['recovery']]['adjusted_return'] - results[pair['rapid_fall']]['adjusted_return']
        rows.append(dict(recovery=pair['recovery'], rapid_fall=pair['rapid_fall'],
                         adjusted_advantage=delta, recovery_wins=delta > 0,
                         originally_unfavorable=not pair['recovery_wins']))
    return rows


def run_audit(classifier=ROOT / 'build-release/aura_incentive_fixture_classifier'):
    archive = json.loads((ROOT / 'docs/traces/step129-incentives/summary.json').read_text())
    sequences, metadata = load_sequences(ROOT / 'docs/traces/step128-physical', classifier)
    candidates = candidate_utilities(ROOT / 'docs/traces/step128-rewards')
    scores = horizon_scores(sequences, metadata, 1200)
    if set(scores) != set(archive['results']):
        raise ValueError('Must reuse all 106 archived cases')
    for name, row in scores.items():
        if {k: v for k, v in row.items() if k != 'records'} != archive['results'][name]:
            raise ValueError(f'Step 129 original objective changed: {name}')
    primary = {}
    for name, candidate in candidates.items():
        results = {case: adjust_case(row, metadata[case]['fork_step'], candidate['value'], .995, 1200)
                   for case, row in scores.items()}
        paired = comparisons(results, archive['recovery_comparisons'])
        primary[name] = dict(results=results, comparisons=paired,
                             remaining_unfavorable=[r for r in paired if not r['recovery_wins']],
                             previously_unfavorable_fixed=sum(
                                 r['originally_unfavorable'] and r['recovery_wins'] for r in paired))
    sensitivity = []
    # Fixed Reward V2 shaping gamma remains .995; other gammas only reprice its
    # unchanged reward samples. This is utility sensitivity, not training config.
    for horizon in (300, 600, 900, 1200):
        rows = scores if horizon == 1200 else horizon_scores(sequences, metadata, horizon)
        for gamma in (.99, .995, .999, 1.):
            for candidate_name, candidate in candidates.items():
                results = {case: adjust_case(row, metadata[case]['fork_step'], candidate['value'], gamma, horizon)
                           for case, row in rows.items()}
                paired = comparisons(results, archive['recovery_comparisons'])
                remaining = [r['recovery'] for r in paired if not r['recovery_wins']]
                sensitivity.append(dict(horizon=horizon, gamma=gamma, candidate=candidate_name,
                                        compared=len(paired), still_favoring_quick_fall=remaining,
                                        note='Short caps may interrupt artificial recovery; gamma sensitivity reprices fixed .995-shaped rewards.'))
    return dict(model='diagnostic_absorbing_failure_tail_v1', reward_version=2,
                horizon=1200, gamma=.995,
                terminal_indexing='Retain r_t; virtual utilities at t+1 through H; correction at terminal is gamma*r_f*sum(gamma**k).',
                candidates=candidates, original_case_count=len(scores), original_scores_exact=True,
                original_metadata=archive['metadata'], primary=primary, sensitivity=sensitivity,
                original_source_sha256=archive['source_sha256'], no_training_started=True,
                balanced_reference_reward=1 + RewardV2Config().recovery_weight * (.995 - 1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = run_audit()
    args.output.mkdir(parents=True, exist_ok=False)
    table = []
    for candidate, data in result['primary'].items():
        for case, row in data['results'].items():
            table.append(dict(candidate=candidate, case=case, **row))
    with (args.output / 'cases.csv').open('w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=list(table[0]));
        writer.writeheader();
        writer.writerows(table)
    paired = [dict(candidate=name, **pair) for name, data in result['primary'].items() for pair in data['comparisons']]
    with (args.output / 'comparisons.csv').open('w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=list(paired[0]));
        writer.writeheader();
        writer.writerows(paired)
    sources = ['python/failure_tail.py', 'python/diagnostics/absorbing_failure_audit.py', 'python/balance_reward_v2.py',
               'python/train_nn.py', 'include/aura/sim/Termination.hpp']
    result['source_sha256'] = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources}
    (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(
        json.dumps({name: dict(utility=result['candidates'][name]['value'], fixed=data['previously_unfavorable_fixed'],
                               still_unfavorable=len(data['remaining_unfavorable'])) for name, data in
                    result['primary'].items()}, indent=2))


if __name__ == '__main__':
    main()
