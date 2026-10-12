"""Drive AURA with a simple alternating hip and knee gait."""

import math
import time

from python.control_aura import write_action

HIP_AMPLITUDE_RAD = 0.3
KNEE_FLEXION_RAD = 0.6
GAIT_ANGULAR_FREQUENCY_RAD_S = 2.0
UPDATE_INTERVAL_S = 0.1


def gait_targets(elapsed_time_s: float) -> tuple[float, float, float, float]:
    """Return left/right hip and knee targets for the current gait phase."""
    phase = GAIT_ANGULAR_FREQUENCY_RAD_S * elapsed_time_s
    swing = math.sin(phase)

    left_hip = HIP_AMPLITUDE_RAD * swing
    right_hip = -left_hip

    # Bend each knee during its forward swing; knees only bend in the positive direction.
    left_knee = KNEE_FLEXION_RAD * max(0.0, swing)
    right_knee = KNEE_FLEXION_RAD * max(0.0, -swing)

    return left_hip, right_hip, left_knee, right_knee


def main() -> None:
    start_time = time.monotonic()

    try:
        while time.monotonic() - start_time < 10.0:
            elapsed_time_s = time.monotonic() - start_time
            targets = gait_targets(elapsed_time_s)
            write_action(*targets)
            time.sleep(UPDATE_INTERVAL_S)
    except KeyboardInterrupt:
        print("\nStopping automatic controller.")
    finally:
        write_action(0.0, 0.0, 0.0, 0.0)
        print("All joint targets returned to neutral.")


if __name__ == "__main__":
    main()
