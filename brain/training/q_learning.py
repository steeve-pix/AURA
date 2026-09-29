import random
from pathlib import Path

from brain.training.aura_env import AuraEnv
from brain.training.reward import standing_reward, has_fallen

ACTIONS = [-1.0, -0.5, 0.0, 0.5, 1.0]
ERROR_STATE_COUNT = 5
RATE_STATE_COUNT = 3
STATE_COUNT = ERROR_STATE_COUNT * RATE_STATE_COUNT
ALPHA = 0.1
GAMMA = 0.95
EPSILON = 0.2

# Rows combine balance-error and error-rate buckets; columns are torque actions.
Q_TABLE = [[0.0 for _ in ACTIONS] for _ in range(STATE_COUNT)]


def error_state(observation):
    error = observation["balance_error"]

    if error < -0.2:
        return 0
    if error < -0.05:
        return 1
    if error < 0.05:
        return 2
    if error < 0.2:
        return 3

    return 4


def rate_state(observation):
    rate = observation["balance_error_rate"]

    if rate < -0.1:
        return 0
    if rate > 0.1:
        return 2
    return 1


def state_from_observation(observation):
    error_bucket = error_state(observation)
    rate_bucket = rate_state(observation)
    return error_bucket * RATE_STATE_COUNT + rate_bucket


def choose_action(q_table, state):
    if random.random() < EPSILON:
        return random.randrange(len(ACTIONS))

    return greedy_action(q_table, state)


def greedy_action(q_table, state):
    if all(value == 0.0 for value in q_table[state]):
        return ACTIONS.index(0.0)

    return max(
        range(len(ACTIONS)),
        key=lambda action_index: q_table[state][action_index],
    )


def update_q_value(q_table, state, action_index, reward, next_state, done):
    current_q = q_table[state][action_index]
    if done:
        target = reward
    else:
        best_next_q = max(q_table[next_state])
        target = reward + GAMMA * best_next_q

    q_table[state][action_index] = current_q + ALPHA * (target - current_q)


def evaluate_greedy_policy(env, max_steps=300):
    observation = env.reset(push_x=0.0)
    total_reward = 0.0

    for step in range(max_steps):
        state = state_from_observation(observation)
        action_index = greedy_action(Q_TABLE, state)
        torque = ACTIONS[action_index]

        observation = env.step(torque, torque)
        total_reward += standing_reward(observation)

        if has_fallen(observation):
            return total_reward, step + 1

    return total_reward, max_steps


def train(episodes=300, max_steps=300):
    project_root = Path(__file__).resolve().parents[2]
    env = AuraEnv(project_root / "build" / "aura")
    state_visits = [0 for _ in range(STATE_COUNT)]

    try:
        for episode in range(episodes):
            push_x = random.uniform(-0.5, 0.5)
            observation = env.reset(push_x=push_x)
            state = state_from_observation(observation)
            total_reward = 0.0

            for step in range(max_steps):
                state_visits[state] += 1
                action_index = choose_action(Q_TABLE, state)
                torque = ACTIONS[action_index]

                next_observation = env.step(torque, torque)
                reward = standing_reward(next_observation)
                next_state = state_from_observation(next_observation)
                done = has_fallen(next_observation)

                update_q_value(
                    Q_TABLE,
                    state,
                    action_index,
                    reward,
                    next_state,
                    done,
                )
                total_reward += reward

                if done:
                    state_visits[next_state] += 1
                    break

                observation = next_observation
                state = next_state

            print("episode:", episode, "reward:", f"{total_reward:.2f}", "steps:", step + 1)

        print("State visits:")
        for state, visits in enumerate(state_visits):
            print("state", state, "visits", visits)

        print("Final Q-table:")
        for state, values in enumerate(Q_TABLE):
            print(state, values)

        greedy_reward, greedy_steps = evaluate_greedy_policy(env, max_steps)
        print("learned policy reward:", greedy_reward)
        print("learned policy steps:", greedy_steps)
    finally:
        env.close()


if __name__ == "__main__":
    train()
