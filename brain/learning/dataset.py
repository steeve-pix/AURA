import random
import torch

from brain.experience import Experience
from brain.learning.features import encode_experience


def build_dataset(experiences: list[Experience]) -> tuple[list[list[float]], list[float]]:
    x = []
    y = []

    for e in experiences:
        if e.kind != "action":
            continue

        feature = encode_experience(e)

        x.append(feature)
        y.append(e.reward)

    return x, y


def train_test_split(x: list[list[float]], y: list[float], test_ratio: float = 0.2, seed: int = 42):
    indices = list(range(len(x)))

    random.Random(seed).shuffle(indices)

    test_count = int(len(indices) * test_ratio)

    test_indices = indices[:test_count]
    train_indices = indices[test_count:]

    x_train = [x[i] for i in train_indices]
    y_train = [y[i] for i in train_indices]

    x_test = [x[i] for i in test_indices]
    y_test = [y[i] for i in test_indices]

    return x_train, y_train, x_test, y_test

def to_tensors(x: list[list[float]], y: list[float])->tuple[torch.Tensor, torch.Tensor]:
    x_tensor = torch.tensor(x, dtype=torch.float32)
    y_tensor = torch.tensor(y, dtype=torch.float32).unsqueeze(1)

    return x_tensor, y_tensor