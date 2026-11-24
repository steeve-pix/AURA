"""Shared deterministic test fixtures; no production behavior."""
import torch

class ShortEnvironment:
    episode_limit = 777
    observation_size = 17

    def __init__(self):
        self.episode = 0
        self.actions = []

    def reset(self):
        self.episode += 1
        self.steps = 0
        return torch.zeros(self.observation_size), {}

    def step(self, action):
        self.actions.append(action)
        self.steps += 1
        done = self.steps == self.episode % 3 + 1
        return torch.zeros(self.observation_size), 1., done, False, dict(
            steps=self.steps, pelvis_height=3., pelvis_uprightness=1.,
            terminated=done, truncated=False, termination_reason="fallen")
