"""Simple fixed-target and feedback balance experiments."""

import aura_python
import random
import sys


def evaluate(simulation: aura_python.AuraSimulation, target: float, on_step=None) -> int | None:
    """Score a constant hip target. on_step is optional viewer/UI feedback."""
    obs = simulation.reset()
    action = {
        "left_hip_target": target,
        "right_hip_target": target,
        "left_knee_target": 0.0,
        "right_knee_target": 0.0,
    }
    if on_step is not None and not on_step(obs):
        return None
    while not obs["done"]:
        obs = simulation.step_action(action)
        if on_step is not None and not on_step(obs):
            return None
    return obs["steps"]


def evaluate_feedback(simulation: aura_python.AuraSimulation, kp: float, kd: float,
                      on_step=None, *, bias: float = 0.0,
                      knee_target: float = 0.0,
                      lateral_gain: float = 0.0) -> int | None:
    """Score feedback with fixed knee target and differential hip feedback."""
    obs = simulation.reset()
    if on_step is not None and not on_step(obs, 0.0, False):
        return None
    while not obs["done"]:
        up_z = obs["pelvis_up"][2]
        omega_x = obs["pelvis_angular_velocity"][0]
        correction = bias + kp * up_z - kd * omega_x
        up_x = obs["pelvis_up"][0]
        lateral = lateral_gain * up_x
        left_hip = max(-0.2, min(0.2, correction + lateral))
        right_hip = max(-0.2, min(0.2, correction - lateral))
        clamped = abs(correction + lateral) > 0.2 or abs(correction - lateral) > 0.2
        obs = simulation.step_action({
            "left_hip_target": left_hip,
            "right_hip_target": right_hip,
            "left_knee_target": knee_target,
            "right_knee_target": knee_target,
        })
        if on_step is not None and not on_step(obs, correction, clamped):
            return None
    return obs["steps"]


def search_feedback() -> tuple[int, float, float, float]:
    """Search locally around the best known gains and hip-target bias."""
    simulation = aura_python.AuraSimulation()
    rng = random.Random(44)
    best_kp = 0.329405
    best_kd = 0.035556
    best_bias = 0.0
    best_score = evaluate_feedback(simulation, best_kp, best_kd, bias=best_bias)
    print(f"Starting score: {best_score}", flush=True)

    for attempt in range(30):
        kp = best_kp + rng.uniform(-0.1, 0.1)
        kd = best_kd + rng.uniform(-0.05, 0.05)
        bias = best_bias + rng.uniform(-0.15, 0.15)
        score = evaluate_feedback(simulation, kp, kd, bias=bias)
        print(
            f"Attempt {attempt + 1:02d}: "
            f"Kp={kp:+.4f}, "
            f"Kd={kd:+.4f}, "
            f"bias={bias:+.4f}, "
            f"survived={score}",
            flush=True,
        )
        if score > best_score:
            best_score = score
            best_kp = kp
            best_kd = kd
            best_bias = bias
            print("  NEW BEST!", flush=True)

    print(f"\nBest survival: {best_score}", flush=True)
    print(f"Best Kp: {best_kp:.6f}", flush=True)
    print(f"Best Kd: {best_kd:.6f}", flush=True)
    print(f"Best bias: {best_bias:.6f}", flush=True)
    return best_score, best_kp, best_kd, best_bias


def main() -> None:
    # The CLI keeps its dashboard/replay interface; direct runs execute the
    # no-viewer local-search experiment and print its measured result.
    if "--events" in sys.argv or "--interactive" in sys.argv:
        if __package__:
            from .balance_session import run_search
        else:
            from balance_session import run_search
        run_search(evaluate_feedback)
        return

    search_feedback()


if __name__ == "__main__":
    main()
