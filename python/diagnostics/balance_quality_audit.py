"""Read archived physical traces and report experimental quality; no simulation/training."""
import argparse
import csv
import hashlib
import json
from dataclasses import asdict
from pathlib import Path
from statistics import mean

from python.balance_quality import BalanceQualityConfig, QUALITY_VERSION, balance_quality_components


def observation(row):
    """Decode the archived Step 128 physical schema without importing a trainer."""
    def f(key):
        return float(row[key])
    return dict(steps=int(row['step']), balance_state=row['balance_state'],
                terminated=bool(int(row['terminated'])),
                done=bool(int(row['terminated']) or int(row['truncated'])),
                pelvis_height=f('pelvis_height'), torso_height=f('torso_height'),
                pelvis_uprightness=f('pelvis_up'), torso_uprightness=f('torso_up'),
                center_of_mass_velocity=[f('com_v' + axis) for axis in 'xyz'],
                pelvis_angular_velocity=[f('pelvis_w' + axis) for axis in 'xyz'],
                torso_angular_velocity=[f('torso_w' + axis) for axis in 'xyz'],
                left_foot_grounded=bool(int(row['left_foot'])),
                right_foot_grounded=bool(int(row['right_foot'])),
                support_margin=f('support_margin'),
                support_polygon=[[float(x) for x in vertex.split(':')]
                                 for vertex in row['polygon'].split(';') if vertex],
                neutral_height_references={key: f('neutral_' + column) for key, column in
                    (('pelvis', 'pelvis'), ('torso', 'torso'), ('center_of_mass', 'com'))})


def audit(source, destination):
    destination.mkdir(parents=True, exist_ok=False)
    reports = []
    for path in sorted(source.glob('*.csv')):
        if path.name.startswith('fixture-'):
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        with path.open() as file:
            rows = list(csv.DictReader(file))
        samples = []
        for row in rows:
            obs = observation(row)
            q = balance_quality_components(obs)
            if q != balance_quality_components(obs):
                raise AssertionError('Nondeterministic quality')
            samples.append(dict(step=obs['steps'], state=obs['balance_state'], **q))
            # Include terminal physical quality for inspection, not a reward payment.
            if obs['done']:
                break
        with (destination / path.name).open('w', newline='') as file:
            writer = csv.DictWriter(file, fieldnames=list(samples[0]))
            writer.writeheader()
            writer.writerows(samples)
        def summary(selected):
            return dict(samples=len(selected), mean=mean(x['balance_quality'] for x in selected)
                        if selected else None)
        reports.append(dict(trace=path.name, source_sha256=digest, **summary(samples),
                            states={s: summary([x for x in samples if x['state'] == s])
                                    for s in ('BALANCED', 'UNBALANCED', 'FALLEN')},
                            terminal_quality=samples[-1]['balance_quality'],
                            terminated=observation(rows[len(samples)-1])['terminated']))
        if digest != hashlib.sha256(path.read_bytes()).hexdigest():
            raise AssertionError('Source trace changed')
    result = dict(quality_version=QUALITY_VERSION, config=asdict(BalanceQualityConfig()),
                  interpretation='Instantaneous quality, not reward; includes reset and terminal sample.',
                  limitations=['Geometric support is not measured load bearing.',
                               'Archived recovery attempts do not establish active motor recovery.',
                               'No return-ranking or training readiness claim.'], traces=reports)
    (destination / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    result = audit(args.source, args.destination)
    print(json.dumps(result, indent=2))
