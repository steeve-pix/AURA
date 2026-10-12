"""Simple fixed-target and feedback balance experiments."""

import random

import aura_python


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
                      on_step=None) -> int | None:
    """Score feedback gains using pelvis lean and angular velocity."""
    obs = simulation.reset()
    if on_step is not None and not on_step(obs, 0.0, False):
        return None
    while not obs["done"]:
        up_z = obs["pelvis_up"][2]
        omega_x = obs["pelvis_angular_velocity"][0]
        correction = kp * up_z - kd * omega_x
        clamped = abs(correction) > 0.2
        correction = max(-0.2, min(0.2, correction))
        obs = simulation.step_action({
            "left_hip_target": correction,
            "right_hip_target": correction,
            "left_knee_target": 0.0,
            "right_knee_target": 0.0,
        })
        if on_step is not None and not on_step(obs, correction, clamped):
            return None
    return obs["steps"]



def search_feedback() -> tuple[int, float, float]:
    """Compare the neutral baseline with 20 seeded feedback gain pairs."""
    simulation = aura_python.AuraSimulation()
    rng = random.Random(42)
    best_score = evaluate_feedback(simulation, 0.0, 0.0)
    best_kp = 0.0
    best_kd = 0.0
    print(f"Baseline: {best_score} steps", flush=True)

    for attempt in range(20):
        kp = rng.uniform(-0.5, 0.5)
        kd = rng.uniform(-0.15, 0.15)
        score = evaluate_feedback(simulation, kp, kd)
        print(f"Attempt {attempt + 1:02d}: Kp={kp:+.3f}, Kd={kd:+.3f}, survived={score}",
              flush=True)
        if score > best_score:
            best_score = score
            best_kp = kp
            best_kd = kd

    print(f"\nBest survival: {best_score} steps", flush=True)
    print(f"Best Kp: {best_kp:+.6f}", flush=True)
    print(f"Best Kd: {best_kd:+.6f}", flush=True)
    return best_score, best_kp, best_kd


def main() -> None:
    # The CLI dashboard and viewer are kept outside the controller experiments.
    if __package__:
        from .balance_session import run_search
    else:
        from balance_session import run_search
    run_search(evaluate_feedback)


if __name__ == "__main__":
    main()
