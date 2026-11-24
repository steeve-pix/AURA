"""Step 131: diagnostic stall audit for unchanged Reward V2 + failure tail.

Native Step 128 traces are passive physics replays. Boundary and recovery
scenarios prescribe measurements and pass through the existing C++ classifier;
they are not asserted to be actuator- or force-feasible.
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
from python.diagnostics.absorbing_failure_audit import candidate_utilities
from python.balance_reward_v2 import BalanceRewardV2, RewardV2Config
from python.failure_tail import discounted_failure_tail
from python.diagnostics.reward_incentive_audit import (append_hold, append_ramp, classify, fixture,
                                           read_trace, score, synthetic_cases)
from python.train_nn import DISCOUNT_FACTOR, discounted_returns

HORIZON = 1200
PHYSICAL_DIR = ROOT / 'docs/traces/step128-physical'
CLASSIFIER = ROOT / 'build-release/aura_incentive_fixture_classifier'


def controlled_scenarios(refs):
    """Create rate-described measurement fixtures; the C++ classifier labels them."""
    good = fixture(refs)
    scenarios, metadata = {}, {}

    def add(name, sequence, interpretation):
        scenarios[name] = sequence
        metadata[name] = dict(kind='constructed_measurements', fork_step=0,
                              interpretation=interpretation,
                              feasibility='Classifier and Reward V2 are real; prescribed body measurements do not prove joint, contact-force, or action feasibility.')

    # Controlled crouch: supported and upright, but below the classifier's
    # standing-height envelope. Reward is measured independently of that label.
    crouch = fixture(refs, angle=5., margin=.15, height_ratio=.62)
    crouch_sequence = [copy.deepcopy(good)]
    append_hold(crouch_sequence, good, 18)
    append_ramp(crouch_sequence, crouch, 120)
    append_hold(crouch_sequence, crouch, HORIZON - len(crouch_sequence) + 1)
    add('controlled_crouch', crouch_sequence,
        'Supported upright crouch at 62% neutral height after a two-second measurement ramp; classifier state is reported separately.')

    # Oscillate around the BALANCED exit envelope (25 degree torso/pelvis
    # retention) and re-entry envelope (15/20 degrees), using ramps slow enough
    # that angular velocity remains a meaningful reward measurement.
    near = fixture(refs, angle=14., margin=.02)
    outside = fixture(refs, angle=26., margin=.02)
    cycle = [copy.deepcopy(good)]
    append_hold(cycle, good, 18)
    while len(cycle) < HORIZON + 1:
        append_ramp(cycle, outside, 24)
        append_hold(cycle, outside, 6)
        append_ramp(cycle, near, 24)
        append_hold(cycle, near, 18)
    cycle = cycle[:HORIZON + 1]
    cycle[-1].update(truncated=True, done=True, termination_reason='time_limit')
    add('oscillate_near_balanced_boundary', cycle,
        'Repeated supported 14-to-26-degree lean cycles around the classifier stay/entry boundary; ramps are measurement interpolation, not an action replay.')

    near_standing = fixture(refs, angle=19., margin=.02)
    steady_near = [copy.deepcopy(good)]
    append_hold(steady_near, good, 18)
    append_ramp(steady_near, near_standing, 120)
    append_hold(steady_near, near_standing, HORIZON - len(steady_near) + 1)
    add('constant_near_balanced_posture', steady_near,
        'Supported, motionless 19-degree posture above the strict BALANCED entry threshold but within its retention-angle band.')

    # A near-fall state stays just below the 60-degree collapse-angle threshold.
    # Heights are below collapse ratios, but the classifier does not count deep
    # collapse while torso tilt remains below that angular condition.
    near_fall = fixture(refs, angle=59., margin=-.1, height_ratio=.24)
    near_fall.update(left_foot_grounded=False, right_foot_grounded=False, support_polygon=[])
    constant = [copy.deepcopy(good)]
    append_hold(constant, good, 18)
    append_ramp(constant, near_fall, 180)
    append_hold(constant, near_fall, HORIZON - len(constant) + 1)
    add('constant_near_fall_stall', constant,
        'Unsupported 59-degree posture below the classifier collapse-angle condition, reached over three seconds and held to truncation.')

    # The oscillation crosses 60 degrees only briefly. Collapse persistence
    # must reset between pulses; the fixture is intentionally adversarial.
    collapse_edge = fixture(refs, angle=61., margin=-.1, height_ratio=.24)
    collapse_edge.update(left_foot_grounded=False, right_foot_grounded=False, support_polygon=[])
    edge_base = copy.deepcopy(near_fall)
    oscillation = [copy.deepcopy(good)]
    append_hold(oscillation, good, 18)
    append_ramp(oscillation, edge_base, 180)
    low_angle = copy.deepcopy(near_fall)
    while len(oscillation) < HORIZON + 1:
        append_ramp(oscillation, collapse_edge, 12)
        append_ramp(oscillation, low_angle, 12)
    oscillation = oscillation[:HORIZON + 1]
    oscillation[-1].update(truncated=True, done=True, termination_reason='time_limit')
    add('oscillate_near_fallen_boundary', oscillation,
        'Unsupported low posture oscillating 59-to-61 degrees; brief collapse-threshold crossings test the classifier persistence rule.')

    # Slow collapse reaches a deeply tilted, low but nonterminal posture. It
    # remains below the 60-degree collapse-angle condition through the cap.
    slow_target = fixture(refs, angle=55., margin=-.02, height_ratio=.4)
    slow = [copy.deepcopy(good)]
    append_hold(slow, good, 18)
    append_ramp(slow, slow_target, 240)
    append_hold(slow, slow_target, HORIZON - len(slow) + 1)
    add('slow_collapse_below_terminal_angle', slow,
        'Two-second supported lean/height ramp to 55 degrees and 40% height, then held below the 60-degree deep-collapse condition.')

    # Confirmed failure follows the same initial standing history and then a
    # measured collapse. Actual termination comes from the C++ classifier.
    unstable = fixture(refs, angle=35., margin=-.1)
    collapsed = fixture(refs, angle=90., margin=-.2, height_ratio=.2)
    collapsed.update(left_foot_grounded=False, right_foot_grounded=False, support_polygon=[])
    fall = [copy.deepcopy(good)]
    append_hold(fall, good, 18)
    append_ramp(fall, unstable, 24)
    append_hold(fall, unstable, 6)
    append_ramp(fall, collapsed, 24)
    append_hold(fall, collapsed, 24)
    if len(fall) < HORIZON + 1:
        append_hold(fall, collapsed, HORIZON + 1 - len(fall))
    add('confirmed_collapse', fall,
        'Common standing prefix followed by measured collapse; the native classifier determines FALLEN and physical termination.')

    return scenarios, metadata


def measure_sequence(sequence, failure_utility=-.65, gamma=DISCOUNT_FACTOR):
    evaluator = BalanceRewardV2()
    evaluator.reset(sequence[0])
    records = []
    for obs in sequence[1:]:
        result = evaluator.update(obs)
        q = result['reward_qualities']
        records.append(dict(step=obs['steps'], state=obs['balance_state'],
                            terminated=obs['terminated'], truncated=obs['truncated'],
                            reward=result['reward_total'],
                            posture_quality=q['posture'], motion_penalty=1. - q['motion'], support_quality=q['support'],
                            **result['reward_components']))
        if obs['done']:
            break
    if not records or not (records[-1]['terminated'] or records[-1]['truncated']):
        raise ValueError('Scenario must terminate or reach the common time limit')
    rewards = [row['reward'] for row in records]
    terminal = records[-1]
    tail_at_terminal = discounted_failure_tail(terminal['step'], fallen=terminal['terminated'],
                                               failure_utility=failure_utility, horizon=HORIZON, gamma=gamma)
    tail_at_start = gamma ** (terminal['step'] - 1) * tail_at_terminal
    physical_discounted = discounted_returns(rewards, gamma)[0]
    state_counts = {state: sum(row['state'] == state for row in records)
                    for state in ('BALANCED', 'UNBALANCED', 'FALLEN')}
    n = len(records)
    return dict(records=records, steps=n, terminal_step=terminal['step'],
                terminated=terminal['terminated'], truncated=terminal['truncated'],
                state_steps=state_counts, state_seconds={k: v / 120 for k, v in state_counts.items()},
                undiscounted_physical_reward=math.fsum(rewards),
                undiscounted_failure_tail=failure_utility * max(0, HORIZON - terminal['step']) if terminal[
                    'terminated'] else 0.,
                undiscounted_adjusted_return=math.fsum(rewards) + (
                    failure_utility * max(0, HORIZON - terminal['step']) if terminal['terminated'] else 0.),
                discounted_physical_return=physical_discounted,
                terminal_penalty_undiscounted=math.fsum(row['terminal'] for row in records),
                terminal_penalty_discounted=math.fsum(gamma ** i * row['terminal'] for i, row in enumerate(records)),
                failure_tail_at_terminal=tail_at_terminal, failure_tail_at_start=tail_at_start,
                discounted_adjusted_return=physical_discounted + tail_at_start,
                mean_posture_quality=math.fsum(row['posture_quality'] for row in records) / n,
                mean_uncontrolled_motion_penalty=math.fsum(row['motion_penalty'] for row in records) / n,
                mean_support_quality=math.fsum(row['support_quality'] for row in records) / n,
                discounted_reward_components={key: math.fsum(gamma ** i * row[key] for i, row in enumerate(records))
                                              for key in RewardV2Config().weights})


def run_audit(classifier=CLASSIFIER):
    if DISCOUNT_FACTOR != .995 or RewardV2Config().discount != DISCOUNT_FACTOR:
        raise ValueError('Step 131 requires the unchanged .995 training and shaping discount')
    step130 = json.loads((ROOT / 'docs/traces/step130-absorbing-failure/summary.json').read_text())
    step130_verification = json.loads((ROOT / 'docs/traces/step130-absorbing-failure/verification.json').read_text())
    if (step130.get('model') != 'diagnostic_absorbing_failure_tail_v1' or
            step130.get('original_case_count') != 106 or not step130.get('original_scores_exact') or
            not step130_verification.get('original_106_case_results_exact')):
        raise ValueError('Saved Step 130 audit does not verify its 106-case baseline')
    archive = json.loads((ROOT / 'docs/traces/step129-incentives/summary.json').read_text())
    physical = {path.stem: read_trace(path) for path in sorted(PHYSICAL_DIR.glob('*.csv'))
                if not path.name.startswith('fixture-')}
    refs = physical['neutral'][0]['neutral_height_references']
    all_fixtures, fixture_metadata = synthetic_cases(refs)
    reused_names = ('sustained_standing_reference', 'severe_recover_wait0_ramp60',
                    'severe_recover_wait720_ramp60', 'severe_unbalanced_truncation')
    reused = {name: all_fixtures[name] for name in reused_names}
    custom, custom_metadata = controlled_scenarios(refs)
    classified = classify({**reused, **custom}, classifier)
    for name in reused_names:
        actual = score(classified[name], fixture_metadata[name]['fork_step'])
        if {key: value for key, value in actual.items() if key != 'records'} != archive['results'][name]:
            raise ValueError(f'Previous constructed scenario changed: {name}')
    sequences = {**physical, **{'physical_' + name: sequence for name, sequence in physical.items()}, **classified}
    selected = {
        'sustained_controlled_standing': ('sustained_standing_reference', 'constructed_measurements',
                                          'Ideal fully supported neutral standing reused from Step 129; not a learned controller trajectory.'),
        'prompt_recovery': ('severe_recover_wait0_ramp60', 'constructed_measurements',
                            'Step 129 shared-prefix artificial 60-tick recovery ramp followed by ideal neutral standing.'),
        'delayed_recovery': ('severe_recover_wait720_ramp60', 'constructed_measurements',
                             'Step 129 shared-prefix 720-tick unstable hold, artificial recovery ramp, then ideal standing.'),
        'constant_unbalanced_posture': ('severe_unbalanced_truncation', 'constructed_measurements',
                                        'Step 129 shared-prefix 35-degree unsupported-margin posture held until truncation.')}
    scenarios = {}
    details = {}
    for label, (source, kind, interpretation) in selected.items():
        scenarios[label] = sequences[source]
        details[label] = dict(source_case=source, kind=kind, interpretation=interpretation,
                              fork_step=0,
                              feasibility=(fixture_metadata[source].get('assumptions', '') if source in fixture_metadata
                                           else 'Recorded native physics measurements; passive controller only.'))
    for name in custom:
        scenarios[name] = sequences[name]
        details[name] = custom_metadata[name]
    for name, sequence in physical.items():
        label = 'native_physics_' + name
        scenarios[label] = sequence
        details[label] = dict(source_case=name, kind='integrated_physics_replay', fork_step=0,
                              interpretation='Step 128 native passive zero-target physics replay; not a learned policy trajectory.',
                              feasibility='Integrated simulator trace with its original measured state and termination labels.')
    candidates = candidate_utilities(ROOT / 'docs/traces/step128-rewards')
    candidate_values = {'stationary_collapse': candidates['stationary_collapse']['value'],
                        'observed_collapse': candidates['observed_collapse']['value'],
                        'physical_lower_bound': candidates['physical_lower_bound']['value']}
    results = {}
    for name, sequence in scenarios.items():
        results[name] = {}
        for utility_name, utility in candidate_values.items():
            results[name][utility_name] = measure_sequence(sequence, utility, .995)
    sensitivity = []
    for name, sequence in scenarios.items():
        for gamma in (.99, .995, .999, 1.):
            for utility_name in ('stationary_collapse', 'physical_lower_bound'):
                utility = candidate_values[utility_name]
                row = measure_sequence(sequence, utility, gamma)
                sensitivity.append(dict(scenario=name, gamma=gamma, utility=utility_name,
                                        discounted_adjusted_return=row['discounted_adjusted_return']))
    source_paths = ['python/balance_reward_v2.py', 'python/failure_tail.py', 'python/diagnostics/absorbing_failure_audit.py',
                    'python/diagnostics/reward_incentive_audit.py', 'python/train_nn.py', 'python/aura_env.py',
                    'src/sim/BalanceClassifier.cpp', 'include/aura/sim/BalanceClassifier.hpp',
                    'include/aura/sim/Termination.hpp', 'python/model_checkpoint.py',
                    'tests/tools/incentive_fixture_classifier.cpp']
    return dict(model='unbalanced_stall_audit_v1', horizon=HORIZON, gamma=DISCOUNT_FACTOR,
                reward_config=RewardV2Config().__dict__, failure_utility_candidates=candidates,
                scenarios=details, results=results, sensitivity=sensitivity,
                step130_saved_106_case_verification_validated=True,
                step129_scenarios_replayed_exactly=len(reused_names),
                failure_tail_semantics='Only confirmed FALLEN; correction at actual termination. UNBALANCED truncation receives no tail.',
                no_training_started=True, audit_uses_only_saved_physics_traces=True,
                fixture_classifier_physics_steps=0,
                source_sha256={path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in source_paths})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    protected = ['python/aura_env.py', 'python/balance_reward_v2.py', 'python/train_nn.py',
                 'python/model_checkpoint.py', 'python/aura_policy.py', 'include/aura/sim/BalanceClassifier.hpp',
                 'include/aura/sim/Termination.hpp', 'src/sim/BalanceClassifier.cpp', 'src/sim/AuraSimulation.cpp']
    checkpoint_paths = sorted((ROOT / 'models').rglob('*.pt'))
    before_sources = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in protected}
    before_models = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in
                     checkpoint_paths}
    result = run_audit()
    after_sources = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in protected}
    after_models = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in
                    sorted((ROOT / 'models').rglob('*.pt'))}
    changed_sources = [path for path in before_sources if before_sources[path] != after_sources.get(path)]
    changed_models = [path for path in before_models if before_models[path] != after_models.get(path)]
    if changed_sources or changed_models or set(before_models) != set(after_models):
        raise RuntimeError('Protected code or model checkpoints changed during the read-only audit')
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for scenario, utilities in result['results'].items():
        for candidate, metrics in utilities.items():
            row = {key: value for key, value in metrics.items() if
                   key not in ('records', 'state_steps', 'state_seconds', 'discounted_reward_components')}
            rows.append(dict(scenario=scenario, candidate=candidate, **row,
                             balanced_seconds=metrics['state_seconds']['BALANCED'],
                             unbalanced_seconds=metrics['state_seconds']['UNBALANCED'],
                             fallen_seconds=metrics['state_seconds']['FALLEN'],
                             discounted_terminal_penalty=metrics['discounted_reward_components']['terminal']))
    with (args.output / 'scenarios.csv').open('w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]));
        writer.writeheader();
        writer.writerows(rows)
    with (args.output / 'sensitivity.csv').open('w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=list(result['sensitivity'][0]));
        writer.writeheader();
        writer.writerows(result['sensitivity'])
    for name, utilities in result['results'].items():
        path = args.output / (name + '.csv')
        with path.open('w', newline='') as file:
            records = utilities['physical_lower_bound']['records']
            writer = csv.DictWriter(file, fieldnames=list(records[0]));
            writer.writeheader();
            writer.writerows(records)
    result[
        'comparison_note'] = 'Scenario initial conditions differ for crouch and threshold-stall fixtures; compare as task outcomes, not paired policy effects.'
    summary = copy.deepcopy(result)
    for utilities in summary['results'].values():
        for metrics in utilities.values(): metrics.pop('records', None)
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (args.output / 'verification.json').write_text(json.dumps(dict(
        protected_source_hashes_unchanged=not changed_sources,
        checkpoint_count=len(before_models), changed_checkpoints=changed_models,
        checkpoint_files_available=bool(before_models),
        no_training_started=True, audit_uses_only_saved_physics_traces=True,
        fixture_classifier_physics_steps=0, cpp_regression_tests_stepped_simulation=True,
        step130_saved_verification_validated=result['step130_saved_106_case_verification_validated']), indent=2) + '\n')
    print(json.dumps({name: {candidate: dict(discounted=round(row['discounted_adjusted_return'], 3),
                                             balanced_s=row['state_seconds']['BALANCED'],
                                             unbalanced_s=row['state_seconds']['UNBALANCED'],
                                             fall=row['terminated'], tail=round(row['failure_tail_at_start'], 3))
                             for candidate, row in values.items()} for name, values in result['results'].items()},
                     indent=2))


if __name__ == '__main__':
    main()
