"""Read-only comparison of paired --projection-momentum-trace CSV outputs.

Usage: python3 tests/compare_projection_momentum_traces.py one.csv four.csv
Reports the fixed 0.1 kg m/s momentum and 1 rad/s angular-speed crossings.
COM displacement is measured separately; pose projection need not conserve COM.
No simulation state, thresholds or input files are modified.
"""
import csv
import json
import math
import sys


def load(path):
    data = dict(stages={}, order=[], sources={}, floors={}, points={}, peak=None)
    with open(path) as file:
        for row in csv.reader(file):
            if not row or not row[0].startswith("projection"):
                continue
            try:
                if row[0] == "projectionMomentumStage":
                    key = (float(row[1]), int(row[2]), row[3])
                    value = dict(momentum=list(map(float, row[4:7])),
                                 angularOwner=row[7], angularSpeed=float(row[8]),
                                 com=list(map(float, row[9:12])))
                    data['stages'][key] = value
                    data['order'].append(key)
                elif row[0] == "projectionMomentumSource":
                    data['sources'].setdefault(float(row[1]), {})[row[2]] = list(map(float, row[3:6]))
                elif row[0] == "projectionFloorResponse":
                    key = (float(row[1]), int(row[2]))
                    data['floors'].setdefault(key, []).append(dict(
                        body=row[3], contactCount=int(row[4]), lowestBefore=float(row[5]), lowestAfter=float(row[6]),
                        momentumBefore=list(map(float, row[7:9])), momentumAfter=list(map(float, row[9:11])),
                        velocityBefore=list(map(float, row[11:14])), velocityAfter=list(map(float, row[14:17])),
                        omegaBefore=float(row[17]), omegaAfter=float(row[18]),
                        normalPy=float(row[19]), dampingPx=float(row[20]), dampingPz=float(row[21])))
                elif row[0] == "projectionFloorPoint":
                    key = (float(row[1]), int(row[2]), row[3])
                    data['points'].setdefault(key, []).append(dict(index=int(row[4]),
                        position=list(map(float, row[5:8])), normalImpulseY=float(row[8])))
                elif row[0] == "projectionAngularPeak":
                    data['peak'] = dict(time=float(row[1]), iteration=int(row[2]), body=row[3],
                        operation=row[4], speed=float(row[5]), replayMismatches=int(row[6].split('=')[1]))
            except ValueError:
                if row[1] != 'time':
                    raise
    return data


def horizontal(value):
    return math.hypot(value[0], value[2])


def difference(a, b):
    return [x-y for x, y in zip(a, b)]


def floor_detail(data, time, iteration):
    records = []
    for contact in data['floors'].get((time, iteration), []):
        records.append(dict(contact, points=data['points'].get((time, iteration, contact['body']), [])))
    key = (time, iteration, 'afterFloor')
    index = data['order'].index(key)
    before = data['stages'][data['order'][index-1]]['momentum']
    after = data['stages'][key]['momentum']
    return dict(iteration=iteration, momentumBefore=[before[0], before[2]],
                momentumAfter=[after[0], after[2]], contacts=records)


def event(key, one, four):
    time, iteration, stage = key
    return dict(time=time, iteration=iteration, stage=stage,
                one=one['stages'][key], four=four['stages'][key],
                horizontalMomentumDifference=horizontal(difference(four['stages'][key]['momentum'], one['stages'][key]['momentum'])),
                angularSpeedDifference=abs(four['stages'][key]['angularSpeed']-one['stages'][key]['angularSpeed']),
                floorOne=floor_detail(one, time, max(1, iteration)),
                floorFour=floor_detail(four, time, max(1, iteration)))


def audit(data):
    closure = 0
    displacement = {name: [0., 0., 0.] for name in ('integration', 'floor', 'jointGeometry', 'thighGeometry', 'room')}
    maxima = {name: 0. for name in ('integration', 'floor', 'anchorImpulse', 'angularLimitImpulse', 'geometry', 'thighImpulse', 'room', 'motors')}
    totals = {name: [0., 0., 0.] for name in maxima}
    for time, sources in data['sources'].items():
        begin = data['stages'][(time, 0, 'beginStep')]['momentum']
        end = data['stages'][(time, 16, 'endStep')]['momentum']
        delta = difference(end, begin)
        summed = [sum(value[i] for value in sources.values()) for i in range(3)]
        closure = max(closure, max(abs(x-y) for x, y in zip(delta, summed)))
        for name, value in sources.items():
            maxima[name] = max(maxima[name], horizontal(value))
            totals[name] = [x+y for x, y in zip(totals[name], value)]
    previous = None
    for key in data['order']:
        current = data['stages'][key]
        stage = key[2]
        category = {'afterIntegration': 'integration', 'afterFloor': 'floor',
                    'afterForwardJoints': 'jointGeometry', 'afterBackwardJoints': 'jointGeometry',
                    'afterThighGeometry': 'thighGeometry', 'afterRoom': 'room'}.get(stage)
        if previous is not None and category:
            delta = difference(current['com'], previous['com'])
            displacement[category] = [x+y for x, y in zip(displacement[category], delta)]
        previous = current
    initial_com = data['stages'][data['order'][0]]['com']
    final_com = data['stages'][data['order'][-1]]['com']
    com_closure = max(abs(final_com[i]-initial_com[i]-sum(value[i] for value in displacement.values())) for i in range(3))
    worst_floor = max((dict(value, time=key[0], iteration=key[1])
                       for key, values in data['floors'].items() for value in values),
                      key=lambda x: x['omegaAfter']-x['omegaBefore'])
    peak = data['peak']
    peak_contact = floor_detail(data, peak['time'], peak['iteration'])
    max_com = max((dict(time=key[0], iteration=key[1], stage=key[2], position=value['com'],
                        radius=math.sqrt(sum(x*x for x in value['com'])),
                        horizontalRadius=math.hypot(value['com'][0], value['com'][2]))
                   for key, value in data['stages'].items()), key=lambda value: value['radius'])
    return dict(momentumLedgerMaximumClosureError=closure, COMDisplacementLedgerMaximumClosureError=com_closure, maximumStepHorizontalChangeBySource=maxima,
                totalMomentumChangeBySource=totals, totalCOMDisplacementBySource=displacement,
                final=data['stages'][data['order'][-1]], angularPeak=peak,
                angularPeakFloorContacts=peak_contact, largestFloorAngularSpeedIncrease=worst_floor, maximumCOMRadius=max_com)


def compare(one, four):
    assert one['order'] == four['order'], 'Run stage ordering differs'
    first_p = next((key for key in one['order'] if horizontal(difference(
        four['stages'][key]['momentum'], one['stages'][key]['momentum'])) > 0.1), None)
    first_omega = next((key for key in one['order'] if abs(
        four['stages'][key]['angularSpeed']-one['stages'][key]['angularSpeed']) > 1.0), None)
    first_step_change = None
    for time in one['sources']:
        def change(data):
            return difference(data['stages'][(time, 16, 'endStep')]['momentum'], data['stages'][(time, 0, 'beginStep')]['momentum'])
        if horizontal(difference(change(four), change(one))) > 0.1:
            first_step_change = (time, 16, 'endStep')
            break
    result = dict(momentumThreshold=0.1, angularSpeedThreshold=1.0,
                  firstMomentumDifference=event(first_p, one, four) if first_p else None,
                  firstStepMomentumChangeDifference=event(first_step_change, one, four) if first_step_change else None,
                  firstAngularSpeedDifference=event(first_omega, one, four) if first_omega else None,
                  one=audit(one), four=audit(four))
    if first_step_change:
        time = first_step_change[0]
        def step_delta(data):
            return difference(data['stages'][(time, 16, 'endStep')]['momentum'], data['stages'][(time, 0, 'beginStep')]['momentum'])
        delta = difference(step_delta(four), step_delta(one))
        result['firstStepMomentumChangeDifference']['stepMomentumChangeDifference'] = delta
        result['firstStepMomentumChangeDifference']['stepMomentumChangeDifferenceMagnitude'] = horizontal(delta)
    for mode in ('one', 'four'):
        assert result[mode]['momentumLedgerMaximumClosureError'] < 1e-7, 'Momentum source accounting does not close'
        assert result[mode]['COMDisplacementLedgerMaximumClosureError'] < 1e-7, 'COM displacement accounting does not close'
        assert result[mode]['angularPeak']['replayMismatches'] == 0, 'Copied contact does not match live velocities'
    return result


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    print(json.dumps(compare(load(sys.argv[1]), load(sys.argv[2])), indent=2))
