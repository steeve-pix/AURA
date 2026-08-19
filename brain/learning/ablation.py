import torch

from brain.learning.features import (
    ABLATION_NAMES,
    FEATURE_GROUPS,
    FEATURE_NAMES
)
from brain.learning.model import ValueModel
from brain.learning.train import evaluate_model, train_model


def without_feature(
        inputs: torch.Tensor,
        feature_name: str,
) -> torch.Tensor:
    if feature_name not in ABLATION_NAMES:
        raise ValueError(f"Unknown model feature: {feature_name}")

    if inputs.ndim != 2 or inputs.shape[1] != len(FEATURE_NAMES):
        raise ValueError(
            f"Expected inputs with shape [N, {len(FEATURE_NAMES)}]."
        )

    names_to_remove = FEATURE_GROUPS.get(feature_name, (feature_name,))
    indices_to_remove = [FEATURE_NAMES.index(name) for name in names_to_remove]

    ablated_inputs = inputs.clone()
    ablated_inputs[:, indices_to_remove] = 0.0

    return ablated_inputs


def evaluate_feature_ablation(
        *,
        feature_name: str,
        x_train: torch.Tensor,
        y_train: torch.Tensor,
        x_test: torch.Tensor,
        y_test: torch.Tensor,
        sample_weights: torch.Tensor,
        epochs: int,
        seed: int,
) -> float:
    x_train_ablated = without_feature(x_train, feature_name)
    x_test_ablated = without_feature(x_test, feature_name)

    torch.manual_seed(seed)
    model = ValueModel()
    train_model(
        model,
        x_train_ablated,
        y_train,
        epochs=epochs,
        sample_weights=sample_weights,
    )

    return evaluate_model(
        model,
        x_test_ablated,
        y_test,
    )
