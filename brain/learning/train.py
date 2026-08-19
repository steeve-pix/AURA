from collections import defaultdict

import torch
from torch import nn

from brain.experience import Experience
from brain.learning.model import ValueModel


def train_model(
        model: ValueModel,
        x_train: torch.Tensor,
        y_train: torch.Tensor,
        epochs: int = 100,
        sample_weights: torch.Tensor | None = None,
) -> list[float]:
    if (
            sample_weights is not None
            and sample_weights.shape != y_train.shape
    ):
        raise ValueError(
            "Sample weights and training targets must have the same shape."
        )

    loss_fn = nn.MSELoss(reduction="none")
    optimizer = torch.optim.Adam(model.parameters(), lr=0.001)
    losses = []

    model.train()

    for _ in range(epochs):
        optimizer.zero_grad()
        predictions = model(x_train)
        errors = loss_fn(predictions, y_train)

        weighted_errors = (
            errors
            if sample_weights is None
            else errors * sample_weights
        )
        loss = weighted_errors.mean()
        loss.backward()
        optimizer.step()
        losses.append(loss.item())

    return losses


def evaluate_model(
        model: ValueModel,
        x_test: torch.Tensor,
        y_test: torch.Tensor,
) -> float:
    loss_fn = nn.MSELoss()
    model.eval()

    with torch.no_grad():
        predictions = model(x_test)
        loss = loss_fn(predictions, y_test)

    return loss.item()


def baseline_mse(
        y_train: torch.Tensor,
        y_test: torch.Tensor,
) -> float:
    mean_reward = y_train.mean()
    predictions = torch.full_like(
        y_test,
        mean_reward.item(),
    )
    loss_fn = nn.MSELoss()

    return loss_fn(predictions, y_test).item()


def per_action_baseline_mse(train_experiences, test_experiences) -> float:
    reward_by_action = defaultdict(list)

    for experience in train_experiences:
        if experience.kind != "action":
            continue

        reward_by_action[experience.action].append(experience.reward)

    action_means = {
        action: sum(rewards) / len(rewards)
        for action, rewards in reward_by_action.items()
    }

    squared_errors = []

    for experience in test_experiences:
        if experience.kind != "action":
            continue

        prediction = action_means[experience.action]

        error = (prediction - experience.reward) ** 2
        squared_errors.append(error)

    return sum(squared_errors) / len(squared_errors)


def predict(
        model: ValueModel,
        x: torch.Tensor) -> torch.Tensor:
    model.eval()

    with torch.no_grad():
        return model(x)
