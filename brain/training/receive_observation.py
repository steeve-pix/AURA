import json
import subprocess
from pathlib import Path

project_root = Path(__file__).resolve().parents[2]
aura_exe = project_root / "build" / "aura"

result = subprocess.run(
    [str(aura_exe), "--observation-once"],
    capture_output=True,
    text=True,
    check=True,
)

observation = json.loads(result.stdout)

print(observation["balance_error"])
print(observation["torso_angle"])
print(observation["left_foot_contact"])
