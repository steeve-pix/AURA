import json
import subprocess
from pathlib import Path

project_root = Path(__file__).resolve().parents[2]
aura_exe = project_root / "build" / "aura"
action = {
    "left_ankle_torque": 1.0,
    "right_ankle_torque": 1.0,
}

result = subprocess.run(
    [str(aura_exe), "--action-once"],
    input=json.dumps(action),
    capture_output=True,
    text=True,
    check=True,
)

observation = json.loads(result.stdout)
print(observation)
