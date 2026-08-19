import argparse
import math
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import torch

from brain.experience import Experience
from brain.experience_analysis import load_experiences
from brain.learning.dataset import build_dataset, to_tensors
from brain.learning.diagnostics import (
    ACTION_NAMES,
    ActionDiagnostics,
    calculate_action_diagnostics,
)
from brain.learning.model import ValueModel
from brain.learning.train import (
    baseline_mse,
    evaluate_model,
    predict,
    train_model, per_action_baseline_mse,
)

DEFAULT_TRAIN_SEEDS = tuple(range(2001, 2009))
DEFAULT_TEST_SEEDS = tuple(range(2009, 2013))
DEFAULT_MINIMUM_FAILURES = 100
DEFAULT_MINIMUM_INVESTIGATIONS = 300


@dataclass(frozen=True)
class ExperimentResult:
    first_train_loss: float
    final_train_loss: float
    test_loss: float
    baseline_loss: float
    action_baseline_loss: float
    training_actions: int
    training_failures: int
    training_investigations: int
    action_diagnostics: dict[str, ActionDiagnostics]


def experience_paths_for_seeds(
        directory: Path,
        seeds: list[int] | tuple[int, ...],
) -> list[Path]:
    paths = []

    for seed in seeds:
        matches = sorted(
            directory.glob(f"maze_{seed}_*.jsonl")
        )

        if len(matches) != 1:
            raise ValueError(
                f"Expected one experience file for seed {seed}, "
                f"found {len(matches)}."
            )

        paths.append(matches[0])

    return paths


def load_experience_files(paths: list[Path]) -> list[Experience]:
    experiences = []

    for path in paths:
        experiences.extend(load_experiences(path))

    return experiences


def validate_training_data(
        experiences: list[Experience],
        *,
        minimum_failures: int = DEFAULT_MINIMUM_FAILURES,
        minimum_investigations: int = DEFAULT_MINIMUM_INVESTIGATIONS,
) -> tuple[int, int, int]:
    actions = [
        experience
        for experience in experiences
        if experience.kind == "action"
    ]
    failures = sum(
        not experience.succeeded
        for experience in actions
    )
    investigations = sum(
        experience.action == "investigate"
        for experience in actions
    )

    shortages = []

    if failures < minimum_failures:
        shortages.append(
            f"{failures}/{minimum_failures} failed actions"
        )

    if investigations < minimum_investigations:
        shortages.append(
            f"{investigations}/{minimum_investigations} investigations"
        )

    if shortages:
        raise ValueError(
            "Training data is not ready: " + ", ".join(shortages) + "."
        )

    return len(actions), failures, investigations


def run_experiment(
        train_paths: list[Path],
        test_paths: list[Path],
        *,
        epochs: int = 100,
        seed: int = 42,
        minimum_failures: int = DEFAULT_MINIMUM_FAILURES,
        minimum_investigations: int = DEFAULT_MINIMUM_INVESTIGATIONS,
) -> ExperimentResult:
    if epochs <= 0:
        raise ValueError("Epochs must be positive.")

    overlap = set(train_paths) & set(test_paths)

    if overlap:
        raise ValueError(
            "Training and test experience files must be different."
        )

    train_experiences = load_experience_files(train_paths)
    test_experiences = load_experience_files(test_paths)
    action_count, failure_count, investigation_count = (
        validate_training_data(
            train_experiences,
            minimum_failures=minimum_failures,
            minimum_investigations=minimum_investigations,
        )
    )

    train_action_experiences = [
        experience
        for experience in train_experiences
        if experience.kind == "action"
    ]
    test_action_experiences = [
        experience
        for experience in test_experiences
        if experience.kind == "action"
    ]
    x_train, y_train = build_dataset(train_action_experiences)
    x_test, y_test = build_dataset(test_action_experiences)

    if not x_train or not x_test:
        raise ValueError(
            "Training and test files must both contain action experiences."
        )

    x_train_tensor, y_train_tensor = to_tensors(
        x_train,
        y_train,
    )
    x_test_tensor, y_test_tensor = to_tensors(
        x_test,
        y_test,
    )

    action_counts = Counter(
        experience.action
        for experience in train_action_experiences
    )

    num_action_types = len(action_counts)
    total_actions = sum(action_counts.values())

    action_weights = {
        action: total_actions /(num_action_types * count)
        for action, count in action_counts.items()
    }
    sample_weights = torch.tensor(
        [
            action_weights[experience.action]
            for experience in train_action_experiences
        ],
        dtype=torch.float32,
    ).unsqueeze(1)

    print("Training action counts:", dict(action_counts))
    print("Training action weights:", action_weights)

    torch.manual_seed(seed)
    model = ValueModel()
    losses = train_model(
        model,
        x_train_tensor,
        y_train_tensor,
        epochs=epochs,
        sample_weights=sample_weights,
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

    action_baseline_loss =per_action_baseline_mse(train_action_experiences,test_action_experiences)
    predictions = predict(model, x_test_tensor)
    diagnostics = calculate_action_diagnostics(
        test_action_experiences,
        predictions,
    )

    return ExperimentResult(
        first_train_loss=losses[0],
        final_train_loss=losses[-1],
        test_loss=test_loss,
        baseline_loss=baseline_loss,
        action_baseline_loss=action_baseline_loss,
        training_actions=action_count,
        training_failures=failure_count,
        training_investigations=investigation_count,
        action_diagnostics=diagnostics,
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Train on complete AURA seed files and evaluate on held-out seeds."
        ),
    )
    parser.add_argument(
        "--data-directory",
        type=Path,
        default=Path("data/experiences"),
    )
    parser.add_argument(
        "--train-seeds",
        type=int,
        nargs="+",
        default=DEFAULT_TRAIN_SEEDS,
    )
    parser.add_argument(
        "--test-seeds",
        type=int,
        nargs="+",
        default=DEFAULT_TEST_SEEDS,
    )
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument(
        "--minimum-failures",
        type=int,
        default=DEFAULT_MINIMUM_FAILURES,
    )
    parser.add_argument(
        "--minimum-investigations",
        type=int,
        default=DEFAULT_MINIMUM_INVESTIGATIONS,
    )
    args = parser.parse_args()

    if set(args.train_seeds) & set(args.test_seeds):
        parser.error("Training and test seeds must not overlap.")

    try:
        train_paths = experience_paths_for_seeds(
            args.data_directory,
            args.train_seeds,
        )
        test_paths = experience_paths_for_seeds(
            args.data_directory,
            args.test_seeds,
        )
        result = run_experiment(
            train_paths,
            test_paths,
            epochs=args.epochs,
            minimum_failures=args.minimum_failures,
            minimum_investigations=args.minimum_investigations,
        )
    except ValueError as error:
        parser.error(str(error))

    print(f"Training action experiences: {result.training_actions}")
    print(f"Training failed actions: {result.training_failures}")
    print(f"Training investigations: {result.training_investigations}")
    print(f"First train loss: {result.first_train_loss:.6f}")
    print(f"Final train loss: {result.final_train_loss:.6f}")
    print(f"Neural model test loss: {result.test_loss:.6f}")
    print(f"Mean baseline loss: {result.baseline_loss:.6f}")
    print(f"Per-action baseline loss: {result.action_baseline_loss:.6f}")

    print("\nValue model diagnostics:")

    for action_name in ACTION_NAMES:
        diagnostics = result.action_diagnostics.get(action_name)

        if diagnostics is None:
            continue

        print(f"\nAction: {action_name}")
        print(f"Count: {diagnostics.count}")
        print(
            "Average actual reward: "
            f"{diagnostics.average_actual_reward:.6f}"
        )
        print(
            "Average predicted reward: "
            f"{diagnostics.average_predicted_reward:.6f}"
        )
        print(f"MSE: {diagnostics.mse:.6f}")


if __name__ == "__main__":
    main()
