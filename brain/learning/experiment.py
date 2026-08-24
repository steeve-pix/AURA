import argparse

from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import torch

from brain.experience import Experience
from brain.experience_analysis import load_experiences
from brain.learning.ablation import evaluate_feature_ablation
from brain.learning.dataset import build_dataset, to_tensors
from brain.learning.diagnostics import (
    ACTION_NAMES,
    ActionDiagnostics,
    calculate_action_diagnostics, ResultRewardDiagnostics, calculate_action_result_rewards, CompletedMoveToDiagnostics,
    calculate_completed_move_to_diagnostics, calculate_move_to_visit_result_rewards,
)
from brain.learning.model import ValueModel
from brain.learning.features import ABLATION_NAMES
from brain.learning.train import (
    baseline_mse,
    evaluate_model,
    predict,
    train_model, per_action_baseline_mse,
)
from brain.learning.model_io import save_model

DEFAULT_TRAIN_SEEDS = tuple(range(2001, 2009))
DEFAULT_TEST_SEEDS = tuple(range(2009, 2013))
DEFAULT_MINIMUM_FAILURES = 100
DEFAULT_MINIMUM_INVESTIGATIONS = 300
DEFAULT_ABLATIONS = (
    "energy",
    "goal",
    "action",
    "has_target",
    "target_offset",
    "path_length",
    "memory_trust",
    "next_step_was_visited",
)


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
    ablation_losses: dict[str, float]
    move_to_result_diagnostics: dict[str, ResultRewardDiagnostics]
    move_to_visit_result_diagnostics: dict[
        tuple[bool | None, str],
        ResultRewardDiagnostics,
    ]
    completed_move_to_diagnostics: CompletedMoveToDiagnostics


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
        ablation_features: tuple[str, ...] = DEFAULT_ABLATIONS,
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

    move_to_result_diagnostics = calculate_action_result_rewards(train_action_experiences, action="move_to")
    move_to_visit_result_diagnostics = calculate_move_to_visit_result_rewards(train_action_experiences)

    completed_move_to_diagnostics = calculate_completed_move_to_diagnostics(train_action_experiences)

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
        action: total_actions / (num_action_types * count)
        for action, count in action_counts.items()
    }
    sample_weights = torch.tensor(
        [
            action_weights[experience.action]
            for experience in train_action_experiences
        ],
        dtype=torch.float32,
    ).unsqueeze(1)

    torch.manual_seed(seed)
    model = ValueModel()
    losses = train_model(
        model,
        x_train_tensor,
        y_train_tensor,
        epochs=epochs,
        sample_weights=sample_weights,
    )

    model_path = Path("data/models/value_model_1.pt")
    save_model(model, model_path)
    print(f"Model saved at {model_path}")

    test_loss = evaluate_model(
        model,
        x_test_tensor,
        y_test_tensor,
    )
    baseline_loss = baseline_mse(
        y_train_tensor,
        y_test_tensor,
    )

    action_baseline_loss = per_action_baseline_mse(train_action_experiences, test_action_experiences)
    predictions = predict(model, x_test_tensor)
    diagnostics = calculate_action_diagnostics(
        test_action_experiences,
        predictions,
    )

    ablation_losses = {}

    for feature_name in ablation_features:
        ablation_losses[feature_name] = evaluate_feature_ablation(
            feature_name=feature_name,
            x_train=x_train_tensor,
            y_train=y_train_tensor,
            x_test=x_test_tensor,
            y_test=y_test_tensor,
            sample_weights=sample_weights,
            epochs=epochs,
            seed=seed,
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
        ablation_losses=ablation_losses,
        move_to_result_diagnostics=move_to_result_diagnostics,
        move_to_visit_result_diagnostics=(
            move_to_visit_result_diagnostics
        ),
        completed_move_to_diagnostics=completed_move_to_diagnostics,
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
    parser.add_argument(
        "--ablate-features",
        choices=ABLATION_NAMES,
        nargs="+",
        default=DEFAULT_ABLATIONS,
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
            ablation_features=tuple(args.ablate_features),
        )
    except ValueError as error:
        parser.error(str(error))

    print("Training data")
    print(f"  Actions:        {result.training_actions}")
    print(f"  Failed:         {result.training_failures}")
    print(f"  Investigations: {result.training_investigations}")

    print("\nEvaluation")
    print(f"  First weighted train loss: {result.first_train_loss:.6f}")
    print(f"  Final weighted train loss: {result.final_train_loss:.6f}")
    print(f"  Held-out test MSE:          {result.test_loss:.6f}")
    print(f"  Global-mean baseline MSE:   {result.baseline_loss:.6f}")
    print(f"  Per-action baseline MSE:    {result.action_baseline_loss:.6f}")
    print("\n Feature ablations")

    print(
        f"  {'Removed input':<18} "
        f"{'Test MSE':>10} "
        f"{'Difference':>12}"
    )

    print(
        f"  {'None (full model)':<18} "
        f"{result.test_loss:>10.6f} "
        f"{0.0:>+12.6f}"
    )

    for feature_name, loss in result.ablation_losses.items():
        difference = loss - result.test_loss

        print(
            f"  {feature_name:<18} "
            f"{loss:>10.6f} "
            f"{difference:>+12.6f}"
        )

    print("\nPer-action diagnostics")
    print(
        f"  {'Action':<12} {'Count':>6} {'Actual':>10} "
        f"{'Predicted':>10} {'MSE':>10}"
    )

    for action_name in ACTION_NAMES:
        diagnostics = result.action_diagnostics.get(action_name)

        if diagnostics is None:
            continue

        print(
            f"  {action_name:<12} {diagnostics.count:>6} "
            f"{diagnostics.average_actual_reward:>10.6f} "
            f"{diagnostics.average_predicted_reward:>10.6f} "
            f"{diagnostics.mse:>10.6f}"
        )

    print("\nTraining move_to results")

    print(
        f"  {'Result':<12} "
        f"{'Count':>8} "
        f"{'Average reward':>16}"
    )

    for result_name in ("completed", "failed", "unreachable"):
        diagnostics = (
            result.move_to_result_diagnostics.get(
                result_name
            )
        )

        if diagnostics is None:
            print(
                f"  {result_name:<12} "
                f"{0:>8} "
                f"{'n/a':>16}"
            )
            continue

        print(
            f"  {result_name:<12} "
            f"{diagnostics.count:>8,d} "
            f"{diagnostics.average_reward:>+16.3f}"
        )

    print("\nTraining move_to by next-step visit history")

    for visited in (False, True, None):
        label = "unknown" if visited is None else str(visited)
        groups = result.move_to_visit_result_diagnostics

        if not any(key[0] is visited for key in groups):
            continue

        print(f"\n  next_step_was_visited = {label}")
        print(
            f"    {'Result':<12} "
            f"{'Count':>8} "
            f"{'Average reward':>16}"
        )

        for result_name in ("completed", "failed", "unreachable"):
            diagnostics = groups.get(
                (visited, result_name)
            )

            if diagnostics is None:
                print(
                    f"    {result_name:<12} "
                    f"{0:>8} "
                    f"{'n/a':>16}"
                )
                continue

            print(
                f"    {result_name:<12} "
                f"{diagnostics.count:>8,d} "
                f"{diagnostics.average_reward:>+16.3f}"
            )

    move_to = result.completed_move_to_diagnostics

    progress = move_to.average_navigation_progress()

    print("\nTraining completed move_to")
    print(f"  Count:                {move_to.count}")
    print(
        f"  Average reward:       "
        f"{move_to.average_reward():+.3f}"
    )
    print(
        f"  Average progress:     "
        f"{'n/a' if progress is None else f'{progress:.3f}'}"
    )
    print(
        f"  Newly visited cells:  "
        f"{move_to.visited_new_cell_rate():.1%}"
    )
    print(
        f"  Average energy cost:  "
        f"{move_to.average_energy_cost():.3f}"
    )


if __name__ == "__main__":
    main()
