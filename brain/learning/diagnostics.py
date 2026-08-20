from collections import defaultdict
from dataclasses import dataclass

import torch

from brain.experience import Experience

ACTION_NAMES = (
    "move",
    "move_to",
    "investigate",
)


@dataclass(frozen=True)
class ActionDiagnostics:
    count: int
    average_actual_reward: float
    average_predicted_reward: float
    mse: float


class RunningErrorTotals:
    def __init__(self):
        self.count = 0
        self.absolute_error_sum = 0.0
        self.squared_error_sum = 0.0

    def record(self, predicted: float, actual: float) -> None:
        error = predicted - actual

        self.count += 1
        self.absolute_error_sum += abs(error)
        self.squared_error_sum += (error * error)

    def mae(self) -> float:
        if self.count == 0:
            return 0.0

        return self.absolute_error_sum / self.count

    def mse(self) -> float:
        if self.count == 0:
            return 0.0

        return self.squared_error_sum / self.count


class RunningValueDiagnostics:
    def __init__(self):
        self.overall = RunningErrorTotals()
        self.by_action = {
            action_name: RunningErrorTotals() for action_name in ACTION_NAMES
        }

    @property
    def count(self) -> int:
        return self.overall.count

    def record(self, *, action: str, predicted: float, actual: float) -> None:
        self.overall.record(predicted, actual)

        if action not in self.by_action:
            self.by_action[action] = RunningErrorTotals()

        self.by_action[action].record(predicted, actual)

    def mae(self) -> float:
        return self.overall.mae()

    def mse(self) -> float:
        return self.overall.mse()


def calculate_action_diagnostics(
        action_experiences: list[Experience],
        predictions: torch.Tensor,
) -> dict[str, ActionDiagnostics]:
    if any(experience.kind != "action" for experience in action_experiences):
        raise ValueError("Value model diagnostics require action experiences.")

    flat_predictions = predictions.reshape(-1)

    if len(action_experiences) != len(flat_predictions):
        raise ValueError(
            "Experiences and predictions must contain the same number of rows."
        )

    diagnostics = {}

    for action_name in ACTION_NAMES:
        indices = [
            index
            for index, experience in enumerate(action_experiences)
            if experience.action == action_name
        ]

        if not indices:
            continue

        actual = torch.tensor(
            [
                action_experiences[index].reward
                for index in indices
            ],
            dtype=torch.float32,
        )
        predicted = flat_predictions[indices].detach().to(
            dtype=torch.float32,
            device="cpu",
        )
        mse = torch.mean((predicted - actual) ** 2).item()

        diagnostics[action_name] = ActionDiagnostics(
            count=len(indices),
            average_actual_reward=actual.mean().item(),
            average_predicted_reward=predicted.mean().item(),
            mse=mse,
        )

    return diagnostics
