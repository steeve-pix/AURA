import torch
from torch import nn
import aura_python

# This is the neural-policy contract version. The simulator still exposes its
# separate 15-target C++ action schema (currently version 2).
ACTION_SCHEMA_VERSION = 3
EXCLUDED_POLICY_ACTION_KEYS = ("head_target",)
CONTROLLED_ACTION_KEYS = (
    "left_hip_target", "right_hip_target", "left_knee_target", "right_knee_target",
    "left_ankle_target", "right_ankle_target", "waist_target", "neck_target",
    "left_shoulder_target", "right_shoulder_target", "left_elbow_target",
    "right_elbow_target", "left_wrist_target", "right_wrist_target",
)

_SIMULATOR_POLICY_KEYS = tuple(
    spec["key"] for spec in aura_python.action_specs
    if spec["key"] not in EXCLUDED_POLICY_ACTION_KEYS
)
if CONTROLLED_ACTION_KEYS != _SIMULATOR_POLICY_KEYS:
    raise RuntimeError("Controlled policy action order must match the simulator contract without excluded actions")

# These are learning choices, distinct from the body's allowed joint limits.
ACTION_TRANSFORMS = {
    "left_hip_target": (0.2, False),
    "right_hip_target": (0.2, False),
    "left_knee_target": (0.3, True),
    "right_knee_target": (0.3, True),
    "left_ankle_target": (0.2, False),
    "right_ankle_target": (0.2, False),
    # Conservative learning ranges for the upper body. These remain distinct
    # from the physical joint limits in ActionSpec.hpp.
    "waist_target": (0.1, False),
    "neck_target": (0.1, False),
    "left_shoulder_target": (0.3, False),
    "right_shoulder_target": (0.3, False),
    "left_elbow_target": (0.48, True),
    "right_elbow_target": (0.48, True),
    "left_wrist_target": (0.24, False),
    "right_wrist_target": (0.24, False),
}


class AuraPolicy(nn.Module):
    def __init__(self, observation_size, hidden_sizes=(64, 64), exploration_std=0.25):
        super().__init__()
        if observation_size < 1 or any(size < 1 for size in hidden_sizes):
            raise ValueError("Layer sizes must be positive")
        if not 0 < exploration_std < float("inf"):
            raise ValueError("exploration_std must be positive and finite")
        self.exploration_std = exploration_std
        self.sample_bounds = (-1.0, 1.0)
        self.action_transforms = dict(ACTION_TRANSFORMS)
        if tuple(self.action_transforms) != CONTROLLED_ACTION_KEYS:
            raise ValueError("Policy action order must match its controlled-action contract")
        sizes = (observation_size, *hidden_sizes, len(self.action_transforms))
        layers = []
        for inputs, outputs in zip(sizes, sizes[1:]):
            layers.extend((nn.Linear(inputs, outputs), nn.Tanh()))
        self.network = nn.Sequential(*layers)

    def forward(self, observation):
        return self.network(observation)

    def sample_action(self, observation):
        mean = self.forward(observation)

        distribution = torch.distributions.Normal(mean, self.exploration_std)

        sample = distribution.sample()

        log_probability = distribution.log_prob(sample).sum()

        action = outputs_to_action(sample.clamp(*self.sample_bounds).detach(), self.action_transforms)

        return action, log_probability


class AuraValue(nn.Module):
    """Estimate expected future return from one encoded physical state."""

    def __init__(self, observation_size, hidden_sizes=(64, 64)):
        super().__init__()
        if observation_size < 1 or any(size < 1 for size in hidden_sizes):
            raise ValueError("Layer sizes must be positive")
        sizes = (observation_size, *hidden_sizes, 1)
        layers = []
        for inputs, outputs in zip(sizes[:-2], sizes[1:-1]):
            layers.extend((nn.Linear(inputs, outputs), nn.Tanh()))
        layers.append(nn.Linear(sizes[-2], sizes[-1]))
        self.network = nn.Sequential(*layers)

    def forward(self, observation):
        return self.network(observation)


def outputs_to_action(output, transforms=None):
    transforms = ACTION_TRANSFORMS if transforms is None else transforms
    if output.ndim != 1 or output.numel() != len(transforms):
        raise ValueError("Policy output count must match its action transforms")
    if not torch.isfinite(output).all():
        raise ValueError("Policy outputs must be finite")
    values = output.clamp(-1.0, 1.0).tolist()
    return {
        key: (max(0.0, value) if nonnegative else value) * scale
        for value, (key, (scale, nonnegative)) in zip(values, transforms.items())
    }


if __name__ == '__main__':
    import aura_python
    from aura_env import encode_observation

    sim = aura_python.AuraSimulation()
    obs = sim.reset()
    inputs = encode_observation(obs)

    policy = AuraPolicy(inputs.numel())

    with torch.no_grad():
        outputs = policy(inputs)

    action = outputs_to_action(outputs)

    print("Converted action:", action)
