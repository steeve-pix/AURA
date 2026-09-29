from pathlib import Path

from brain.training.aura_env import AuraEnv
from brain.training.reward import standing_reward, has_fallen

ACTIONS = [-1.0, -0.5, 0.0, 0.5, 1.0]

def choose_action(observation):
    error = observation["balance_error"]

    if error < -0.1:
        return 1.0

    if error > 0.1:
        return -1.0

    return 0.0


def evaluate_action(env, torque, max_steps=300):
    observation = env.reset()
    total_reward = 0.0
    steps_taken = 0

    for step in range(max_steps):
        observation = env.step(torque, torque)
        total_reward += standing_reward(observation)
        steps_taken = step + 1

        if has_fallen(observation):
            break

    return total_reward, steps_taken


def evaluate_policy(env, max_steps=300):
    observation = env.reset()
    total_reward = 0.0

    for step in range(max_steps):
        torque = choose_action(observation)
        observation = env.step(torque, torque)
        total_reward += standing_reward(observation)

        if has_fallen(observation):
            return total_reward, step + 1

    return total_reward, max_steps


if __name__ == "__main__":
    project_root = Path(__file__).resolve().parents[2]
    env = AuraEnv(project_root / "build" / "aura")

    try:
        for torque in ACTIONS:
            reward, _ = evaluate_action(env, torque)
            print("torque:", torque, "reward:", reward)

        constant_reward, constant_steps = evaluate_action(env, 1.0)
        policy_reward, policy_steps = evaluate_policy(env)
        print("constant +1 reward:", constant_reward, "steps:", constant_steps)
        print("policy reward:", policy_reward, "steps:", policy_steps)
    finally:
        env.close()
