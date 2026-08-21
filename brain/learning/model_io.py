from pathlib import Path

import torch

from brain.learning.model import ValueModel


def save_model(model: ValueModel, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    torch.save(model.state_dict(), path)


def load_model(path: Path) -> ValueModel:
    model = ValueModel()

    state_dict = torch.load(path, map_location=torch.device('cpu'), weights_only=True)
    model.load_state_dict(state_dict)

    model.eval()

    return model
