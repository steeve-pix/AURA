"""Format AURA's inventory and verify the selected head impulse's tensor response."""
import argparse
import csv
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("inventory", type=Path)
parser.add_argument("event", type=Path)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--table", type=Path, required=True)
args = parser.parse_args()
with args.inventory.open() as f:
    inventory = [r for r in csv.reader(f) if r and r[0] == "massInertia" and r[1] != "part"]
assert len(inventory) == 16
parts = [{"part": r[1], "mass": float(r[2]), "size": list(map(float,r[3:6])),
          "principal_inertia": list(map(float,r[6:9]))} for r in inventory]
for p in parts:
    assert p["mass"] > 0 and min(p["principal_inertia"]) > 0
    x,y,z = p["size"]
    expected = [p["mass"]*(y*y+z*z)/12, p["mass"]*(x*x+z*z)/12,
                p["mass"]*(x*x+y*y)/12]
    assert max(abs(a-b) for a,b in zip(expected,p["principal_inertia"])) < 1e-7
with args.event.open() as f:
    rows = list(csv.reader(f))
impulse = next(r for r in rows if r and r[0] == "headInertiaImpulse")
assert int(impulse[2]) == 1262556
bodies = {}
for r in rows:
    if not r or r[0] != "headInertiaBody":
        continue
    v = list(map(float,r[2:]))
    entry = {"mass": v[0], "principal_inertia": v[1:4],
             "axial_moment": v[4], "directional_response_inertia": v[5],
             "angular_impulse_magnitude": v[6], "lever_arm": v[7:10],
             "angular_impulse": v[10:13], "world_impulse_axis": v[13:16],
             "local_impulse_axis": v[16:19], "tensor_predicted_delta_omega": v[19:22],
             "measured_delta_omega": v[22:25], "scalar_axial_approximation": v[25:28],
             "predicted_delta_omega_magnitude": v[28],
             "measured_delta_omega_magnitude": v[29], "prediction_error": v[30]}
    assert entry["prediction_error"] < 1e-5
    bodies[r[1]] = entry
assert set(bodies) == {"head", "neck"}
report = {"event_time": float(impulse[1]), "operation_index": int(impulse[2]),
          "effective_inverse_mass": float(impulse[3]),
          "calculated_impulse_on_head": list(map(float,impulse[4:7])),
          "impulse_from_measured_delta_v": list(map(float,impulse[7:10])),
          "parts": parts, "total_mass": sum(p["mass"] for p in parts),
          "event_bodies": bodies,
          "note": "Axial moment is n dot I_world n; exact vector response is I_world_inverse times angular impulse."}
args.output.write_text(json.dumps(report,indent=2)+"\n")
table = ["# AURA mass and inertia inventory", "",
         "Mass: kg. Size: m. Local principal moments about each COM: kg·m².", "",
         "| Part | Mass | Size X/Y/Z | Ix | Iy | Iz |",
         "|---|---:|---|---:|---:|---:|"]
for p in parts:
    table.append("| "+p["part"]+" | "+f'{p["mass"]:g}'+" | "+
                 " / ".join(f"{x:g}" for x in p["size"])+" | "+
                 " | ".join(f"{x:.6f}" for x in p["principal_inertia"])+" |")
table += ["", f'**Total mass: {report["total_mass"]:g} kg.**']
args.table.write_text("\n".join(table)+"\n")
print(json.dumps(bodies,indent=2))
