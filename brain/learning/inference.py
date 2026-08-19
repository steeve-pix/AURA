from dataclasses import dataclass

import torch

from brain.learning.features import encode_experience
from brain.learning.model import ValueModel

def predict_value(model: ValueModel, feature_vector: list[float]) -> float:
    model.eval()

    x = torch.tensor(
        [feature_vector],
        dtype=torch.float32
    )

    with torch.no_grad():
        prediction = model(x)

    return prediction.item()
