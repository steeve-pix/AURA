"""Step 129: inspect the unchanged Reward V2 objective; no training or tuning.

Physical branches reuse Step 128 measurements. Synthetic branches prescribe
measurements, not forces/actions: even rate-consistent fixtures are not proof
that AURA can realize a recovery. The native helper supplies all state/end flags.
"""
import argparse
import copy
import csv
import hashlib
import json
import math
import subprocess
import sys
from dataclasses import asdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / 'build-release')]
from python.balance_reward_v2 import BalanceRewardV2, RewardV2Config
from python.diagnostics.reward_diagnostics import observation as trace_observation
from python.train_nn import DISCOUNT_FACTOR, discounted_returns

DT = 1 / 120
LIMIT = 1200


def read_trace(path):
    with path.open() as file:
        rows = list(csv.DictReader(file))
    result = []
    for row in rows:
        obs = trace_observation(row)
        obs['center_of_mass_height'] = float(row['com_height'])
        obs['torso_contact'] = bool(int(row['torso_contact']))
        obs['head_contact'] = bool(int(row['head_contact']))
        result.append(obs)
        if obs['done']:
            break
    return result


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def fixture(refs, angle=0., margin=.2, height_ratio=1.):
    """Prescribed reward measurements; polygon/margin agree with COM projection."""
    up = math.cos(math.radians(angle))
    com_height = refs['center_of_mass'] * height_ratio
    return dict(steps=0, termination_mode='balance_v2', termination_version=2,
                neutral_height_references=refs, pelvis_uprightness=up, torso_uprightness=up,
                pelvis_height=refs['pelvis'] * height_ratio, torso_height=refs['torso'] * height_ratio,
                center_of_mass_height=com_height, center_of_mass_position=[.8 - margin, com_height, 0.],
                center_of_mass_velocity=[0., 0., 0.], pelvis_angular_velocity=[0., 0., 0.],
                torso_angular_velocity=[0., 0., 0.], left_foot_grounded=True, right_foot_grounded=True,
                support_polygon=[[-.8, -.325], [.8, -.325], [.8, .325], [-.8, .325]],
                support_margin=margin, torso_contact=False, head_contact=False)


def append_hold(sequence, target, count):
    for _ in range(count):
        obs = copy.deepcopy(target)
        obs['steps'] = len(sequence)
        sequence.append(obs)


def append_ramp(sequence, target, count):
    """Interpolate sampled measurements; use pose derivatives for motion costs.

    Contacts remain the source contacts until the endpoint. No claim is made
    that interpolated contacts, articulated anchors or force balance are feasible.
    """
    start = sequence[-1]
    previous = start
    for index in range(1, count + 1):
        fraction = index / count
        obs = copy.deepcopy(target if index == count else start)
        for key in ('pelvis_height', 'torso_height', 'center_of_mass_height', 'support_margin'):
            obs[key] = start[key] + fraction * (target[key] - start[key])
        for part in ('pelvis', 'torso'):
            key = part + '_uprightness'
            initial_angle = math.acos(max(-1., min(1., start[key])))
            final_angle = math.acos(max(-1., min(1., target[key])))
            angle = initial_angle + fraction * (final_angle - initial_angle)
            obs[key] = math.cos(angle)
            obs[part + '_angular_velocity'] = [(final_angle - initial_angle) / (count * DT), 0., 0.]
        if 'center_of_mass_position' in start and 'center_of_mass_position' in target:
            obs['center_of_mass_position'] = [a + fraction * (b - a) for a, b in
                                              zip(start['center_of_mass_position'], target['center_of_mass_position'])]
            obs['center_of_mass_velocity'] = [(b - a) / DT for a, b in
                                              zip(previous['center_of_mass_position'], obs['center_of_mass_position'])]
        else:
            obs['center_of_mass_velocity'] = [0.,
                                              (obs['center_of_mass_height'] - previous['center_of_mass_height']) / DT,
                                              0.]
        obs['steps'] = len(sequence)
        sequence.append(obs)
        previous = obs


def synthetic_cases(refs):
    cases, metadata = {}, {}
    good = fixture(refs)
    collapsed = fixture(refs, angle=90., margin=-.2, height_ratio=.2)
    collapsed.update(left_foot_grounded=False, right_foot_grounded=False, support_polygon=[])
    for quality, angle, margin in (('severe', 35., -.1), ('very_severe', 55., -.1), ('mild', 25., .02)):
        unstable = fixture(refs, angle=angle, margin=margin)
        prefix = [copy.deepcopy(good)]
        append_hold(prefix, good, 18)
        append_ramp(prefix, unstable, 24)
        append_hold(prefix, unstable, 6)
        fork = len(prefix) - 1

        def add(name, trajectory, **details):
            cases[name] = trajectory
            metadata[name] = dict(kind='constructed_measurements', cohort=quality, fork_step=fork,
                                  assumptions='Prescribed measurements; native classification; no proven joint/force/action feasibility.',
                                  **details)

        standing = copy.deepcopy(prefix)
        append_ramp(standing, good, 1)
        append_hold(standing, good, LIMIT - len(standing) + 1)
        add(quality + '_ideal_recovery_reference', standing, recovery_wait=0, recovery_ramp=1,
            interpretation='Artificial one-tick recovery upper reference, not a plausible movement.')
        survive = copy.deepcopy(prefix)
        append_hold(survive, unstable, LIMIT - fork)
        add(quality + '_unbalanced_truncation', survive, interpretation='Prescribed unstable hold to time limit.')
        for wait in (0, 24, 120, 240, 480, 720, 900):
            fall = copy.deepcopy(prefix)
            append_hold(fall, unstable, wait)
            append_ramp(fall, collapsed, 24)
            append_hold(fall, collapsed, 24)
            add(f'{quality}_fall_wait{wait}', fall, collapse_wait=wait, collapse_ramp=24,
                interpretation='Constructed collapse; terminal time supplied by actual classifier.')
            for duration in (24, 60, 120):
                if fork + wait + duration + 18 > LIMIT:
                    continue
                recovered = copy.deepcopy(prefix)
                append_hold(recovered, unstable, wait)
                append_ramp(recovered, good, duration)
                append_hold(recovered, good, LIMIT - len(recovered) + 1)
                add(f'{quality}_recover_wait{wait}_ramp{duration}', recovered,
                    recovery_wait=wait, recovery_ramp=duration,
                    interpretation='Artificial recovery; subsequent ideal standing is an optimistic assumption.')
    standing = [copy.deepcopy(good)]
    append_hold(standing, good, LIMIT)
    cases['sustained_standing_reference'] = standing
    metadata['sustained_standing_reference'] = dict(kind='constructed_measurements', cohort='standing_reference',
                                                    fork_step=0,
                                                    interpretation='Ideal supported standing to time limit; prescribed, not dynamically simulated.')
    # Same common history through 1176. A one-tick change at the cap gives
    # either 24 consecutive collapse samples (FALLEN) or 23 (UNBALANCED).
    for name, collapse_ticks in (('cap_fallen', 24), ('cap_unbalanced', 23)):
        sequence = [copy.deepcopy(good)]
        append_hold(sequence, good, LIMIT - collapse_ticks)
        append_hold(sequence, collapsed, collapse_ticks)
        cases[name] = sequence
        metadata[name] = dict(kind='constructed_measurements', cohort='cap_boundary', fork_step=1176,
                              interpretation='Artificial abrupt pose change; tests existing persistence and cap semantics.')
    return cases, metadata


def classify(cases, binary):
    result = subprocess.run([str(binary)], input=json.dumps(cases), text=True,
                            capture_output=True, check=True)
    labels = json.loads(result.stdout)
    classified = {}
    for name, sequence in cases.items():
        classified[name] = [{**obs, **label} for obs, label in zip(sequence, labels[name])]
    return classified


def score(sequence, fork_step=0):
    evaluator = BalanceRewardV2()
    evaluator.reset(sequence[0])
    records = []
    context = None
    for obs in sequence[1:]:
        if obs['steps'] == fork_step + 1:
            context = digest(dict(observation=sequence[fork_step], support=evaluator.support,
                                  previous_potential=evaluator.previous_potential))
        reward = evaluator.update(obs)
        if obs['steps'] > fork_step:
            records.append(dict(step=obs['steps'], state=obs['balance_state'],
                                terminated=obs['terminated'], truncated=obs['truncated'],
                                reward=reward['reward_total'], **reward['reward_components']))
        if obs['done']:
            break
    if not records or not (records[-1]['terminated'] or records[-1]['truncated']):
        raise ValueError('Audit sequences must end by termination or truncation')
    rewards = [row['reward'] for row in records]
    discounted = discounted_returns(rewards)[0]
    direct = math.fsum(DISCOUNT_FACTOR ** i * value for i, value in enumerate(rewards))
    if not math.isclose(discounted, direct, abs_tol=1e-10, rel_tol=1e-12):
        raise ValueError('Training and direct discounted returns disagree')
    components = {key: math.fsum(DISCOUNT_FACTOR ** i * row[key] for i, row in enumerate(records))
                  for key in RewardV2Config().weights}
    state_counts = {state: sum(row['state'] == state for row in records)
                    for state in ('BALANCED', 'UNBALANCED', 'FALLEN')}
    return dict(total_reward=sum(rewards), discounted_return=discounted, steps=len(records),
                terminal_step=records[-1]['step'], terminated=records[-1]['terminated'],
                truncated=records[-1]['truncated'],
                by_state={state: dict(steps=count, seconds=count * DT) for state, count in state_counts.items()},
                terminal_penalty_sum=sum(row['terminal'] for row in records),
                discounted_components=components, initial_context_sha256=context,
                input_sha256=digest(sequence), records=records)


def extreme_measurements(refs):
    observations = [fixture(refs), fixture(refs, angle=180., height_ratio=.2, margin=-1.)]
    extreme = fixture(refs, angle=179., height_ratio=.2, margin=-1.)
    extreme.update(center_of_mass_velocity=[1e30, -1e30, 1e30],
                   pelvis_angular_velocity=[1e30, 0., 0.], torso_angular_velocity=[1e30, 0., 0.])
    observations.append(extreme)
    result = []
    for obs in observations:
        obs.update(balance_state='UNBALANCED', terminated=False, truncated=False, done=False)
        evaluator = BalanceRewardV2();
        evaluator.reset(obs)
        row = evaluator.update({**obs, 'steps': 1})
        result.append(dict(kind='finite_float32_measurement_stress_not_reachability',
                           reward=row['reward_total'], qualities=row['reward_qualities']))
    return result


def run_audit(trace_dir, classifier):
    if DISCOUNT_FACTOR != .995 or RewardV2Config().discount != DISCOUNT_FACTOR:
        raise ValueError('Audit requires the actual unchanged training/shaping discount .995')
    native = {path.stem: read_trace(path) for path in sorted(trace_dir.glob('*.csv'))
              if not path.name.startswith('fixture-')}
    classified_native = classify(native, classifier)
    for name, sequence in native.items():
        for original, classified in zip(sequence, classified_native[name]):
            for key in ('balance_state', 'terminated', 'truncated', 'done', 'termination_reason'):
                if original[key] != classified[key]:
                    raise ValueError(f'Native classifier replay mismatch: {name} {key}')
        if len(sequence) != len(classified_native[name]):
            raise ValueError('Native classifier replay length mismatch')
    refs = native['neutral'][0]['neutral_height_references']
    fixtures, metadata = synthetic_cases(refs)
    fixtures = classify(fixtures, classifier)
    scores = {name: score(sequence, metadata[name]['fork_step']) for name, sequence in fixtures.items()}
    for quality in ('mild', 'severe', 'very_severe'):
        contexts = {row['initial_context_sha256'] for name, row in scores.items() if
                    metadata[name]['cohort'] == quality}
        if len(contexts) != 1:
            raise ValueError('Constructed branches have different starting/reward memory states')
    for name, sequence in native.items():
        key = 'physical_' + name
        fork = 80 if name in ('neutral', 'brief_airborne', 'rapid_rotation', 'recovery_attempt') else 0
        metadata[key] = dict(kind='integrated_physics_replay',
                             cohort='native_fork80' if fork else 'unpaired_native_postures',
                             fork_step=fork,
                             interpretation='Passive zero-target simulation; recovery is passive settling, not learned control.')
        scores[key] = score(sequence, fork)
    contexts = {scores['physical_' + name]['initial_context_sha256'] for name in
                ('neutral', 'brief_airborne', 'rapid_rotation', 'recovery_attempt')}
    if len(contexts) != 1:
        raise ValueError('Physical fork trajectories do not share initial context')
    comparisons = []
    for name, details in metadata.items():
        if 'recovery_ramp' not in details or 'ideal_' in name:
            continue
        baseline = details['cohort'] + '_fall_wait0'
        delta = scores[name]['discounted_return'] - scores[baseline]['discounted_return']
        comparisons.append(dict(recovery=name, rapid_fall=baseline, discounted_advantage=delta,
                                recovery_wins=delta > 0))
    penalty_thresholds = {}
    for quality in ('mild', 'severe', 'very_severe'):
        quick = scores[quality + '_fall_wait0']
        required = []
        for name, details in metadata.items():
            if details['cohort'] != quality or name == quality + '_fall_wait0':
                continue
            other = scores[name]
            gap = quick['discounted_return'] - other['discounted_return']
            slope = other['discounted_components']['terminal'] - quick['discounted_components']['terminal']
            if gap > 0 and slope > 0:
                required.append(dict(comparison=name, minimum_weight=1 + gap / slope))
        penalty_thresholds[quality] = dict(current_weight=1.,
                                           maximum_required_for_this_grid=max((r['minimum_weight'] for r in required),
                                                                              default=1.),
                                           comparisons=required,
                                           warning='Analytical sensitivity of this fixture grid, not a proposed fitted coefficient.')
    attempts = []
    for wait in (24, 120, 240, 480, 720, 900):
        recover = scores[f'severe_recover_wait{wait}_ramp60']['discounted_return']
        failure = scores[f'severe_fall_wait{wait}']['discounted_return']
        quick = scores['severe_fall_wait0']['discounted_return']
        threshold = (quick - failure) / (recover - failure)
        attempts.append(dict(wait=wait, success_return=recover, failure_return=failure,
                             rapid_fall_return=quick, minimum_success_probability=threshold,
                             interpretation='Constructed binary lottery, not a measured recovery probability.',
                             expected_returns={str(p): p * recover + (1 - p) * failure for p in
                                               (0., .01, .05, .25, .5, 1.)}))
    return dict(gamma=DISCOUNT_FACTOR, config=asdict(RewardV2Config()), metadata=metadata,
                native_classifier_replay_exact=True, recovery_attempt_lotteries=attempts,
                terminal_penalty_sensitivity=penalty_thresholds,
                results=scores, recovery_comparisons=comparisons, extremes=extreme_measurements(refs))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--traces', type=Path, default=ROOT / 'docs/traces/step128-physical')
    parser.add_argument('--classifier', type=Path, default=ROOT / 'build-release/aura_incentive_fixture_classifier')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = run_audit(args.traces, args.classifier)
    args.output.mkdir(parents=True, exist_ok=False)
    selected = {'physical_neutral', 'physical_brief_airborne', 'sustained_standing_reference',
                'severe_fall_wait0', 'severe_fall_wait240', 'severe_recover_wait0_ramp60',
                'severe_recover_wait720_ramp60', 'severe_recover_wait900_ramp60',
                'severe_unbalanced_truncation', 'mild_unbalanced_truncation',
                'very_severe_fall_wait0', 'very_severe_recover_wait480_ramp60',
                'very_severe_unbalanced_truncation', 'cap_fallen', 'cap_unbalanced'}
    for name, row in result['results'].items():
        records = row.pop('records')
        if name in selected:
            with (args.output / (name + '.csv')).open('w', newline='') as file:
                writer = csv.DictWriter(file, fieldnames=list(records[0]))
                writer.writeheader();
                writer.writerows(records)
    table = []
    for name, row in result['results'].items():
        table.append(dict(case=name, kind=result['metadata'][name]['kind'],
                          cohort=result['metadata'][name]['cohort'], fork_step=result['metadata'][name]['fork_step'],
                          total_reward=row['total_reward'], discounted_return=row['discounted_return'],
                          balanced_seconds=row['by_state']['BALANCED']['seconds'],
                          unbalanced_seconds=row['by_state']['UNBALANCED']['seconds'],
                          terminated=row['terminated'], truncated=row['truncated'],
                          discounted_terminal_penalty=row['discounted_components']['terminal']))
    with (args.output / 'cases.csv').open('w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=list(table[0]));
        writer.writeheader();
        writer.writerows(table)
    sources = ['python/balance_reward_v2.py', 'python/train_nn.py', 'python/aura_env.py',
               'src/sim/BalanceClassifier.cpp', 'include/aura/sim/BalanceClassifier.hpp',
               'include/aura/sim/Termination.hpp',
               'python/diagnostics/reward_incentive_audit.py', 'tests/tools/incentive_fixture_classifier.cpp']
    result['source_sha256'] = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources}
    result['trace_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in sorted(args.traces.glob('*.csv')) if not p.name.startswith('fixture-')}
    (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    losing = [r for r in result['recovery_comparisons'] if not r['recovery_wins']]
    print(json.dumps(dict(cases=len(result['results']), recovery_comparisons=len(result['recovery_comparisons']),
                          recovery_loses_to_quick_fall=len(losing), examples=losing[:3]), indent=2))


if __name__ == '__main__':
    main()
