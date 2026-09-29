from pathlib import Path

from brain.training.aura_env import AuraEnv
from brain.training.reward import standing_reward, has_fallen

project_root = Path(__file__).resolve().parents[2]
env = AuraEnv(project_root / "build" / "aura")

observation = env.reset()

for step in range(1000):
    observation = env.step(1.0, 1.0);

    reward = standing_reward(observation)

    print(step, reward)

    if has_fallen(observation):
        print("AURA fell at step", step)
        break

env.close()
