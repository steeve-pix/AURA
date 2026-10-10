"""Summarize the unchanged 2.141667 s head/neck replay and check conservation."""
import argparse
import csv
import json
import math
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("off", type=Path)
parser.add_argument("on", type=Path)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()

def norm(v):
    return math.sqrt(sum(x*x for x in v))

report = {"time": 257/120, "outer_iteration": 4, "modes": {}}
for mode in ("off", "on"):
    with getattr(args, mode).open() as stream:
        rows = list(csv.reader(stream))
    stages = [r for r in rows if r and r[0] == "headAnchorStage" and r[2] == "4"]
    out = {"stages": {}, "intermediate_stages": []}
    for r in stages:
        values = list(map(float, r[6:]))
        entry = {"sweep": r[3], "operation": r[5],
                 "neck_anchor_velocity": values[0:3],
                 "head_anchor_velocity": values[3:6],
                 "relative_velocity": values[6:9],
                 "neck_anchor_world": values[9:12],
                 "head_anchor_world": values[12:15],
                 "relative_speed": values[15]}
        if r[4] == "intermediate":
            out["intermediate_stages"].append(entry)
        else:
            out["stages"].setdefault(r[4], []).append(entry)
    b = out["stages"]["B_beforeHeadFloor"][0]
    c = out["stages"]["C_afterHeadFloor"][0]
    out["floor_head_anchor_delta"] = [y-x for x,y in zip(b["head_anchor_velocity"],c["head_anchor_velocity"])]
    out["floor_neck_anchor_delta"] = [y-x for x,y in zip(b["neck_anchor_velocity"],c["neck_anchor_velocity"])]
    assert out["floor_neck_anchor_delta"] == [0,0,0]
    floor_rows = [r for r in rows if r and r[0] == "headFloorAnchorDelta"]
    assert len(floor_rows) == 4
    v = list(map(float, floor_rows[3][2:]))
    out["floor_decomposition"] = {
        "head_anchor_delta": v[0:3], "neck_anchor_delta": v[3:6],
        "head_delta_v": v[6:9], "head_delta_omega_cross_anchor_lever": v[9:12],
        "damping_delta_v": v[12:15], "damping_delta_omega": v[15:18],
        "normal_delta_v": v[18:21], "normal_delta_omega": v[21:24],
        "contact_count": int(v[24])}
    out["floor_impulse_events"] = [
        r for r in rows if r and r[0] == "angularDynamicsFloor"
        and r[2] == "4" and r[3] == "head"]
    out["room_responses"] = [
        r for r in rows if r and r[0] == "headRoomResponse"]
    audits = [r for r in rows if r and r[0] == "headJointMomentumAudit"]
    # Four head-anchor sub-iterations per forward head visit.
    selected = [r for r in audits if int(r[2]) >= 1262556][:4]
    assert len(selected) == 4
    out["joint_impulse_audits"] = []
    for r in selected:
        v = list(map(float,r[3:]))
        entry = {"operation_index": int(r[2]), "P_before": v[0:3],
                 "L_before": v[3:6], "P_after": v[6:9], "L_after": v[9:12],
                 "delta_P": v[12:15], "delta_L": v[15:18],
                 "impulse_on_head": v[18:21], "anchor_gap_vector": v[21:24],
                 "expected_gap_torque": v[24:27], "delta_KE": v[27]}
        entry["delta_P_norm"] = norm(entry["delta_P"])
        entry["delta_L_norm"] = norm(entry["delta_L"])
        entry["gap_corrected_delta_L_norm"] = norm(
            [x-y for x,y in zip(entry["delta_L"],entry["expected_gap_torque"])])
        assert entry["delta_P_norm"] < 2e-6
        assert entry["gap_corrected_delta_L_norm"] < 1e-5
        assert entry["delta_KE"] < 1e-5
        out["joint_impulse_audits"].append(entry)
    out["preceding_backward_stages"] = [
        r for r in rows if r and r[0] == "headAnchorStage"
        and r[2] == "3" and r[3] == "backward"]
    previous = out["preceding_backward_stages"]
    before = next(r for r in previous if r[4] == "beforeRoom")
    after = next(r for r in previous if r[4] == "afterRoom")
    room = next(r for r in out["room_responses"] if r[2] == "3")
    v = list(map(float, room[3:]))
    out["preceding_room_floor_response"] = {
        "relative_speed_before": float(before[-1]),
        "relative_speed_after": float(after[-1]),
        "head_impulse": v[0:3], "head_delta_omega": v[3:6],
        "neck_impulse": v[6:9], "neck_delta_omega": v[9:12]}
    report["modes"][mode] = out
args.output.write_text(json.dumps(report,indent=2) + "\n")
for mode,out in report["modes"].items():
    print(mode, {k: v[0]["relative_speed"] for k,v in out["stages"].items()})
