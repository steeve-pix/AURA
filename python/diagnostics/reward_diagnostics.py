"""Replay native physical measurements into Reward V2; never train a network.

Run aura_balance_validation into a fresh directory first. Its added angular,
neutral-height and polygon columns are required: older traces cannot supply them.
"""
import argparse
import csv
import hashlib
import json
import sys
from dataclasses import asdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / 'build-release')]
from python.balance_reward_v2 import BalanceRewardV2, RewardV2Config
from python.aura_env import balance_reward


def observation(row):
    def f(key): return float(row[key])

    return dict(termination_mode='balance_v2', termination_version=2,
                steps=int(row['step']), balance_state=row['balance_state'],
                terminated=bool(int(row['terminated'])), truncated=bool(int(row['truncated'])),
                done=bool(int(row['terminated']) or int(row['truncated'])),
                termination_reason=row['termination_reason'],
                pelvis_height=f('pelvis_height'), torso_height=f('torso_height'),
                pelvis_uprightness=f('pelvis_up'), torso_uprightness=f('torso_up'),
                center_of_mass_velocity=[f('com_v' + axis) for axis in 'xyz'],
                pelvis_angular_velocity=[f('pelvis_w' + axis) for axis in 'xyz'],
                torso_angular_velocity=[f('torso_w' + axis) for axis in 'xyz'],
                left_foot_grounded=bool(int(row['left_foot'])), right_foot_grounded=bool(int(row['right_foot'])),
                support_margin=f('support_margin'),
                support_polygon=[[float(x) for x in vertex.split(':')] for vertex in row['polygon'].split(';') if
                                 vertex],
                neutral_height_references={k: f('neutral_' + col) for k, col in
                                           (('pelvis', 'pelvis'), ('torso', 'torso'), ('center_of_mass', 'com'))})


def replay(rows):
    evaluator = BalanceRewardV2();
    evaluator.reset(observation(rows[0]));
    records = []
    for row in rows[1:]:
        obs = observation(row);
        result = evaluator.update(obs)
        records.append({**row, 'reward_total': result['reward_total'],
                        **{'reward_' + key: value for key, value in result['reward_components'].items()},
                        **{'quality_' + key: value for key, value in result['reward_qualities'].items()}})
        if obs['done']: break
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path);
    parser.add_argument('output', type=Path)
    parser.add_argument('--compare', type=Path, default=ROOT / 'docs/traces/step127-scenarios')
    args = parser.parse_args();
    args.output.mkdir(parents=True, exist_ok=False)
    summaries = [];
    unchanged = True
    for path in sorted(args.input.glob('*.csv')):
        if path.name.startswith('fixture-'): continue
        with path.open() as file:
            rows = list(csv.DictReader(file))
        previous = args.compare / path.name
        with previous.open() as file:
            old = list(csv.DictReader(file))
        equal = len(old) == len(rows) and all(
            all(row[k] == oldrow[k] for k in oldrow) for row, oldrow in zip(rows, old))
        if not equal: raise ValueError(f'Previous physical trace changed: {path.name}')
        unchanged &= equal
        records = replay(rows)
        if records != replay(rows): raise ValueError('Reward replay is not deterministic')
        with (args.output / path.name).open('w', newline='') as file:
            writer = csv.DictWriter(file, fieldnames=list(records[0]));
            writer.writeheader();
            writer.writerows(records)

        def phase(subset):
            return dict(steps=len(subset), seconds=len(subset) / 120,
                        average_reward=sum(r['reward_total'] for r in subset) / len(subset) if subset else None)

        states = {state: phase([r for r in records if r['balance_state'] == state])
                  for state in ('BALANCED', 'UNBALANCED', 'FALLEN')}
        legacy = []
        legacy_end = 0 if rows[0]['legacy_standing'] == '0' else None
        for row in ([] if legacy_end == 0 else rows[1:]):
            failed = row['legacy_standing'] == '0'
            legacy.append(balance_reward(dict(terminated=failed, pelvis_uprightness=float(row['pelvis_up']))))
            if failed:
                legacy_end = int(row['step'])
                break
        # The same simulated path, restricted to V2's duration, for a like-time
        # illustration of the old formula. This is not a legacy episode score.
        old_formula_same_path = sum(.5 + .5 * max(0., min(1., float(r['pelvis_up']))) for r in records)
        summaries.append(dict(scenario=path.stem, total_reward=sum(r['reward_total'] for r in records),
                              by_state=states,
                              terminal_reward=records[-1]['reward_total'] if records[-1]['terminated'] == '1' else None,
                              termination_step=int(records[-1]['step']),
                              component_totals={k: sum(r['reward_' + k] for r in records) for k in
                                                RewardV2Config().weights},
                              legacy_episode_reward=sum(legacy), legacy_termination_step=legacy_end,
                              legacy_formula_through_v2_end=old_formula_same_path,
                              timelines=[{k: r[k] for k in
                                          ('step', 'balance_state', 'reward_total', 'reward_recovery', 'quality_motion',
                                           'quality_support')}
                                         for i, r in enumerate(records) if
                                         i == 0 or r['balance_state'] != records[i - 1]['balance_state'] or
                                         int(r['step']) in (50, 80, 81, 100, 124, 146, 180, 200, 238, 300, 400,
                                                            462) or i == len(records) - 1]))
    sources = ['python/balance_reward_v2.py', 'python/aura_env.py', 'python/train_nn.py',
               'src/sim/AuraSimulation.cpp', 'src/sim/BalanceClassifier.cpp', 'include/aura/sim/Termination.hpp']
    result = dict(reward_version=2, termination_mode='balance_v2', config=asdict(RewardV2Config()),
                  scenarios=summaries, physical_columns_identical_to_step127=unchanged,
                  deterministic_reward_replay=True, no_training=True,
                  state_duration_convention='Post-tick labels, including terminal tick; reset is not rewarded',
                  source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources})
    (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(scenarios=len(summaries), unchanged_physics=unchanged,
                          totals={r['scenario']: r['total_reward'] for r in summaries}), indent=2))


if __name__ == '__main__': main()
