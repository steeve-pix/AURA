from collections import defaultdict
from dataclasses import dataclass

import torch

from brain import reward
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


@dataclass(frozen=True)
class ResultRewardDiagnostics:
    count: int
    average_reward: float


class RunningErrorTotals:
    def __init__(self):
        self.count = 0
        self.absolute_error_sum = 0.0
        self.squared_error_sum = 0.0
        self.predicted_sum = 0.0
        self.actual_sum = 0.0

    def record(self, predicted: float, actual: float) -> None:
        error = predicted - actual

        self.count += 1
        self.absolute_error_sum += abs(error)
        self.squared_error_sum += (error * error)
        self.predicted_sum += predicted
        self.actual_sum += actual

    def mae(self) -> float:
        if self.count == 0:
            return 0.0

        return self.absolute_error_sum / self.count

    def mse(self) -> float:
        if self.count == 0:
            return 0.0

        return self.squared_error_sum / self.count

    def mean_prediction(self) -> float:
        if self.count == 0:
            return 0.0

        return self.predicted_sum / self.count

    def mean_actual(self) -> float:
        if self.count == 0:
            return 0.0

        return self.actual_sum / self.count


class RunningValueDiagnostics:
    def __init__(self):
        self.overall = RunningErrorTotals()
        self.by_action = {
            action_name: RunningErrorTotals() for action_name in ACTION_NAMES
        }

        self.by_action_result: dict[
            tuple[str, str], RunningErrorTotals
        ] = {}

    @property
    def count(self) -> int:
        return self.overall.count

    def record(self, *, action: str, result: str, predicted: float, actual: float) -> None:
        self.overall.record(predicted, actual)

        if action not in self.by_action:
            self.by_action[action] = RunningErrorTotals()

        self.by_action[action].record(predicted, actual)

        action_result = (action, result)

        if action_result not in self.by_action_result:
            self.by_action_result[action_result] = RunningErrorTotals()

        self.by_action_result[action_result].record(predicted, actual)

    def mae(self) -> float:
        return self.overall.mae()

    def mse(self) -> float:
        return self.overall.mse()


class CompletedMoveToDiagnostics:
    def __init__(self):
        self.count = 0
        self.reward_sum = 0.0

        self.navigation_progress_sum = 0
        self.navigation_progress_count = 0

        self.visited_new_cell_count = 0
        self.energy_cost_sum = 0

    def record(self, experience: Experience) -> None:
        if experience.kind != "action":
            return

        if experience.action != "move_to":
            return

        if experience.result != "completed":
            return

        self.count += 1
        self.reward_sum += experience.reward

        if experience.navigation_progress is not None:
            self.navigation_progress_sum += experience.navigation_progress
            self.navigation_progress_count += 1

        if experience.visited_new_cell:
            self.visited_new_cell_count += 1

        energy_cost = max(0, experience.energy_before - experience.energy_after)
        self.energy_cost_sum += energy_cost

    def average_reward(self) -> float:
        if self.count == 0:
            return 0.0

        return self.reward_sum / self.count

    def average_navigation_progress(self) -> float | None:
        if self.navigation_progress_count == 0:
            return None

        return self.navigation_progress_sum / self.navigation_progress_count

    def visited_new_cell_rate(self) -> float:
        if self.count == 0:
            return 0.0

        return self.visited_new_cell_count / self.count

    def average_energy_cost(self) -> float:
        if self.count == 0:
            return 0.0

        return self.energy_cost_sum / self.count


def calculate_action_diagnostics(action_experiences: list[Experience],predictions: torch.Tensor) -> dict[str, ActionDiagnostics]:
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


def calculate_action_result_rewards(experiences: list[Experience], *, action: str) -> dict[
    str, ResultRewardDiagnostics]:
    reward_by_result = defaultdict(list)

    for experience in experiences:
        if experience.kind != "action":
            continue

        if experience.action != action:
            continue

        reward_by_result[experience.result].append(experience.reward)

    return {
        result: ResultRewardDiagnostics(
            count=len(reward),
            average_reward=sum(reward) / len(reward),
        ) for result, reward in reward_by_result.items()
    }


def calculate_move_to_visit_result_rewards(experiences: list[Experience]) -> dict[tuple[bool | None, str], ResultRewardDiagnostics]:
    rewards_by_visit_and_result = defaultdict(list)

    for experience in experiences:
        if experience.kind != "action":
            continue

        if experience.action != "move_to":
            continue

        key = (
            experience.next_step_was_visited,
            experience.result,
        )
        rewards_by_visit_and_result[key].append(
            experience.reward
        )

    return {
        key: ResultRewardDiagnostics(
            count=len(rewards),
            average_reward=sum(rewards) / len(rewards),
        )
        for key, rewards in rewards_by_visit_and_result.items()
    }


def calculate_completed_move_to_diagnostics(experiences: list[Experience])->CompletedMoveToDiagnostics:
    diagnostics = CompletedMoveToDiagnostics()

    for experience in experiences:
        diagnostics.record(experience)

    return diagnostics
