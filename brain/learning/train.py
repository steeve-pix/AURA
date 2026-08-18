import torch
from torch import nn

from brain.learning.model import ValueModel


def train_model(
    model: ValueModel,
    x_train: torch.Tensor,
    y_train: torch.Tensor,
    epochs: int = 100,
) -> list[float]:
    loss_fn = nn.MSELoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=0.001)
    losses = []

    model.train()

    for _ in range(epochs):
        optimizer.zero_grad()
        predictions = model(x_train)
        loss = loss_fn(predictions, y_train)
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

def predict(
model:ValueModel,
x:torch.Tensor) -> torch.Tensor:
    model.eval()

    with torch.no_grad():
        return model(x)