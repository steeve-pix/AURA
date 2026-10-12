import json
import time
from pathlib import Path

from control_aura import write_action

PROJECT_DIR = Path(__file__).resolve().parent.parent
OBSERVATION_FILE = PROJECT_DIR / "viewer_observation.json"
kp = 0.2
kd = 0.05

start = time.monotonic()
try:
    while time.monotonic() - start < 10.0:
        with OBSERVATION_FILE.open("r") as file:
            observation = json.load(file)

        up_x, up_y, up_z = observation["pelvis_up"]
        omega_x, omega_y, omega_z = observation["pelvis_angular_velocity"]
        correction = kp * up_z - kd * omega_x
        correction = max(-0.1, min(0.1, correction))
        write_action(correction, correction, 0.0, 0.0)

        print(
            f"Height: {observation['pelvis_height']:.2f} | "
            f"Uprightness: {observation['pelvis_uprightness']:.2f} | "
            f"Standing: {observation['is_standing']} | "
            f"Sideways tilt: {up_x:+.3f} | "
            f"Uprightness: {up_y:+.3f} | "
            f"Forward/back tilt: {up_z:+.3f} | "
            f"Angular velocity: "
            f"X={omega_x:+.3f} | "
            f"Y={omega_y:+.3f} | "
            f"Z={omega_z:+.3f} rad/s"
        )
        print(f"Proposed correction: {correction:+.3f}")

        uprightness = observation["pelvis_uprightness"]

        if uprightness < 0.5:
            print("⚠️ AURA is falling!")

        time.sleep(0.1)
finally:
    write_action(0.0, 0.0)
