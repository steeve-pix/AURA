import argparse
from pathlib import Path

import torch

from brain.experience_analysis import load_experiences
from brain.learning.dataset import (
    build_dataset,
    to_tensors,
    train_test_split,
)
from brain.learning.model import ValueModel
from brain.learning.train import (
    baseline_mse,
    evaluate_model,
    train_model,
)

DEFAULT_EXPERIENCE_PATH = Path(
    "data/experiences/maze_1337_42x21_b12_u20.jsonl"
)


def run_experiment(
        experience_path: Path,
        *,
        epochs: int = 100,
        test_ratio: float = 0.2,
        seed: int = 42,
) -> tuple[float, float, float, float]:
    experiences = load_experiences(experience_path)
    x, y = build_dataset(experiences)

    if len(x) < 2:
        raise ValueError(
            "At least two action experiences are required."
        )

    x_train, y_train, x_test, y_test = train_test_split(
        x,
        y,
        test_ratio=test_ratio,
        seed=seed,
    )

    if not x_train or not x_test:
        raise ValueError(
            "The train/test split must contain data on both sides."
        )

    x_train_tensor, y_train_tensor = to_tensors(
        x_train,
        y_train,
    )
    x_test_tensor, y_test_tensor = to_tensors(
        x_test,
        y_test,
    )

    torch.manual_seed(seed)
    model = ValueModel()

    losses = train_model(
        model,
        x_train_tensor,
        y_train_tensor,
        epochs=epochs,
    )
    test_loss = evaluate_model(
        model,
        x_test_tensor,
        y_test_tensor,
    )
    baseline_loss = baseline_mse(
        y_train_tensor,
        y_test_tensor,
    )

    return (
        losses[0],
        losses[-1],
        test_loss,
        baseline_loss,
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Train and evaluate AURA's value model.",
    )
    parser.add_argument(
        "experience_path",
        type=Path,
        nargs="?",
        default=DEFAULT_EXPERIENCE_PATH,
    )
    parser.add_argument(
        "--epochs",
        type=int,
        default=100,
    )
    args = parser.parse_args()

    first_loss, final_loss, test_loss, baseline_loss, = run_experiment(
        args.experience_path,
        epochs=args.epochs,
    )

    print(f"First train loss: {first_loss:.6f}")
    print(f"Final train loss: {final_loss:.6f}")
    print(f"Neural model test loss: {test_loss:.6f}")
    print(f"Mean baseline loss: {baseline_loss:.6f}")


if __name__ == "__main__":
    main()
