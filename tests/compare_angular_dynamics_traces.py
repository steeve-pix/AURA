"""Compare read-only angular traces; operation indices match the frozen solver order."""
import argparse
import csv
import json
import math
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("off", type=Path)
parser.add_argument("on", type=Path)
parser.add_argument("off_window", type=Path)
parser.add_argument("on_window", type=Path)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()

def rows(path):
    with path.open() as stream:
        return list(csv.reader(stream))

steps = {}
for mode in ("off", "on"):
    steps[mode] = {r[1]: r for r in rows(getattr(args, mode))
                   if r and r[0] == "angularDynamicsStep"}
assert len(steps["off"]) == len(steps["on"]) == 1200
time = next(t for t, r in steps["on"].items()
            if float(r[3]) - float(steps["off"][t][3]) > 5)
windows = {mode: rows(getattr(args, mode + "_window")) for mode in ("off", "on")}
maxima = {mode: {r[2]: r for r in data if r and r[0] == "angularDynamicsMaximum"}
          for mode, data in windows.items()}
index = next(k for k, r in maxima["on"].items()
             if r[1] == time and float(r[8]) - float(maxima["off"][k][8]) > 5)
report = {"criterion": "ON completed-step max omega exceeds OFF by >5 rad/s",
          "first_step_time": float(time), "first_operation_in_that_step": {},
          "completed_step": {}}
for mode, data in windows.items():
    m = maxima[mode][index]
    assert m[1:2] == [time] and m[3:7] == maxima["on"][index][3:7]
    body = m[7]
    r = next(r for r in data if r and r[0] == "angularDynamicsOperation"
             and r[-2] == index and r[6] == body)
    audit = next(r for r in data if r and r[0] == "angularDynamicsAnchorAudit"
                 and r[2] == index)
    impulse = list(map(float, r[20:23]))
    floor = [r for r in data if r and r[0] == "angularDynamicsFloor"
             and r[1] == time and int(r[2]) <= int(m[3])]
    report["completed_step"][mode] = {
        "owner": steps[mode][time][2], "omega": float(steps[mode][time][3])}
    report["first_operation_in_that_step"][mode] = {
        "operation_index": int(index), "outer_iteration": int(m[3]),
        "sweep": m[4], "joint": m[5], "operation": m[6], "body": body,
        "omega_before": float(r[7]), "omega_after": float(r[8]),
        "lowest_y_before": float(r[9]), "omega_vector_before": list(map(float, r[10:13])),
        "omega_vector_after": list(map(float, r[13:16])),
        "orientation_wxyz": list(map(float, r[16:20])),
        "endpoint_impulse": impulse, "impulse_magnitude": math.sqrt(sum(x*x for x in impulse)),
        "anchor_lever_arm": list(map(float, r[23:26])),
        "whole_body_delta_ke": float(r[-1]), "predicted_work": float(audit[4]),
        "measured_work": float(audit[5]), "quadratic_energy": float(audit[6]),
        "anchor_scalar_speed_before": float(audit[8]),
        "anchor_scalar_speed_after": float(audit[9]),
        "endpoint_omega_replay_error": list(map(float, audit[10:12])),
        "preceding_floor_impulses": floor}
args.output.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report["completed_step"], indent=2))
