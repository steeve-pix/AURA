import sys
from typing import TextIO

from brain.experience import Experience
from brain.learning.disagreement_analysis import (
    DisagreementRecord,
    DisagreementStatistics,
)
from brain.learning.candidates import ScoredCandidate, decision_key
from brain.learning.diagnostics import (
    CompletedMoveToDiagnostics,
    RunningValueDiagnostics,
)

DIVIDER = "─" * 78
CANDIDATE_DIVIDER = "─" * 108
BOX_WIDTH = 78
BOX_CONTENT_WIDTH = BOX_WIDTH - 4


def _box_top(title: str) -> str:
    prefix = f"╭─ {title} "
    return prefix + "─" * (BOX_WIDTH - len(prefix) - 1) + "╮"


def _box_line(content: str = "") -> str:
    return f"│ {content:<{BOX_CONTENT_WIDTH}} │"


def _box_separator() -> str:
    return "├" + "─" * (BOX_WIDTH - 2) + "┤"


def _box_bottom() -> str:
    return "╰" + "─" * (BOX_WIDTH - 2) + "╯"


def format_decision(key: tuple) -> str:
    goal, action, direction, target = key

    if direction is not None:
        action_label = f"{action} {direction}"
    elif target is not None:
        action_label = f"{action} {target}"
    else:
        action_label = str(action)

    return f"{goal} → {action_label}"


def format_live_summary(diagnostics: RunningValueDiagnostics, completed_move_to: CompletedMoveToDiagnostics) -> str:
    progress = completed_move_to.average_navigation_progress()
    progress_text = "n/a" if progress is None else f"{progress:.3f}"

    lines = [
        "",
        _box_top("Value Model · Live Accuracy"),
        _box_line(
            f"Samples: {diagnostics.count:,}"
            f"       MAE: {diagnostics.mae():.3f}"
            f"       MSE: {diagnostics.mse():.3f}"
        ),
        _box_separator(),
        _box_line(
            f"{'Action':<14} {'N':>7} {'Actual':>10} "
            f"{'Predicted':>11} {'MAE':>9} {'MSE':>9}"
        ),
    ]

    for action, totals in diagnostics.by_action.items():
        if totals.count == 0:
            continue

        lines.append(_box_line(
            f"{action:<14} {totals.count:>7,d} "
            f"{totals.mean_actual():>+10.3f} "
            f"{totals.mean_prediction():>+11.3f} "
            f"{totals.mae():>9.3f} {totals.mse():>9.3f}"
        ))

    lines.extend([
        _box_separator(),
        _box_line(
            "Completed move_to: "
            f"n={completed_move_to.count:,}  "
            f"reward={completed_move_to.average_reward():+.3f}  "
            f"progress={progress_text}"
        ),
        _box_line(
            f"New-cell rate: {completed_move_to.visited_new_cell_rate():.1%}"
            f"       Average energy cost: "
            f"{completed_move_to.average_energy_cost():.3f}"
        ),
        _box_bottom(),
    ])

    has_failure_result = any(
        result != "completed" and totals.count > 0
        for (_, result), totals in diagnostics.by_action_result.items()
    )

    if has_failure_result:
        lines.extend([
            "",
            _box_top("Value Model · Result Detail"),
            _box_line(
                f"{'Action':<12} {'Result':<12} {'N':>6} "
                f"{'Actual':>9} {'Predicted':>10} {'MAE':>8} {'MSE':>8}"
            ),
        ])

        for (action, result), totals in sorted(diagnostics.by_action_result.items()):
            lines.append(_box_line(
                f"{action:<12} {result:<12} {totals.count:>6,d} "
                f"{totals.mean_actual():>+9.3f} "
                f"{totals.mean_prediction():>+10.3f} "
                f"{totals.mae():>8.3f} {totals.mse():>8.3f}"
            ))

        lines.append(_box_bottom())

    return "\n".join(lines)


def format_candidate_disagreement(scored_candidates: list[ScoredCandidate], *, rule_key: tuple,
                                  model_key: tuple) -> str:
    lines = [
        "",
        "VALUE MODEL · RULE/MODEL DISAGREEMENT",
        CANDIDATE_DIVIDER,
        (
            f"{'CHOICE':<8} {'DECISION':<40} {'REACHABLE':>9} "
            f"{'PATH':>6} {'NEXT VISITED':>12} {'PREDICTED':>10}"
        ),
        (
            f"{'-' * 8} {'-' * 40} {'-' * 9} "
            f"{'-' * 6} {'-' * 12} {'-' * 10}"
        ),
    ]

    for scored in scored_candidates:
        key = decision_key(
            scored.candidate.goal,
            scored.candidate.action,
        )
        labels = []

        if key == rule_key:
            labels.append("RULE")

        if key == model_key:
            labels.append("MODEL")

        choice = "/".join(labels) or ""
        value_input = scored.value_input
        reachable = (
            "n/a"
            if scored.reachable is None
            else "yes" if scored.reachable else "no"
        )
        path = (
            "n/a"
            if value_input is None or value_input.path_length is None
            else str(value_input.path_length)
        )
        next_visited = (
            "n/a"
            if value_input is None
               or value_input.next_step_was_visited is None
            else "yes" if value_input.next_step_was_visited else "no"
        )
        lines.append(
            f"{choice:<8} {format_decision(key):<40} "
            f"{reachable:>9} {path:>6} {next_visited:>12} "
            f"{scored.predicted_reward:>+10.3f}"
        )

    lines.extend([
        CANDIDATE_DIVIDER,
        "The rule decision will execute; the model is advisory only.",
    ])
    return "\n".join(lines)


def format_candidate_scores(
        scored_candidates: list[ScoredCandidate],
        *,
        eligible_keys: set[tuple],
) -> str:
    lines = [
        "",
        "VALUE MODEL · CANDIDATES",
        CANDIDATE_DIVIDER,
        (
            f"{'DECISION':<40} {'ELIGIBLE':>9} {'REACHABLE':>9} "
            f"{'PATH':>6} {'NEXT VISITED':>12} {'VALUE':>10}"
        ),
        CANDIDATE_DIVIDER,
    ]

    for scored in scored_candidates:
        key = decision_key(
            scored.candidate.goal,
            scored.candidate.action,
        )
        value_input = scored.value_input
        reachable = (
            "n/a"
            if scored.reachable is None
            else "yes" if scored.reachable else "no"
        )
        path = (
            "n/a"
            if value_input is None or value_input.path_length is None
            else str(value_input.path_length)
        )
        next_visited = (
            "n/a"
            if value_input is None
               or value_input.next_step_was_visited is None
            else "yes" if value_input.next_step_was_visited else "no"
        )
        eligible = "yes" if key in eligible_keys else "no"

        lines.append(
            f"{format_decision(key):<40} {eligible:>9} "
            f"{reachable:>9} {path:>6} {next_visited:>12} "
            f"{scored.predicted_reward:>+10.3f}"
        )

    lines.append(CANDIDATE_DIVIDER)
    return "\n".join(lines)


def format_disagreement_result(comparison: dict, *, rule_actual: float) -> str:
    rule_prediction = comparison["rule_prediction"]

    return "\n".join([
        "",
        "VALUE MODEL · DISAGREEMENT RESULT",
        DIVIDER,
        f"RULE    {format_decision(comparison['rule'])}",
        (
            f"        predicted {rule_prediction:+.3f}   "
            f"actual {rule_actual:+.3f}   "
            f"error {abs(rule_prediction - rule_actual):.3f}"
        ),
        f"MODEL   {format_decision(comparison['model'])}",
        (
            f"        predicted {comparison['model_prediction']:+.3f}   "
            "actual n/a (not executed)"
        ),
        DIVIDER,
    ])


def format_disagreement_outcome(
        record: DisagreementRecord,
        statistics: DisagreementStatistics,
) -> str:
    rule_target = (
        ""
        if record.rule_target is None
        else f" {record.rule_target}"
    )
    model_target = (
        ""
        if record.model_target is None
        else f" {record.model_target}"
    )
    rule_actual = record.rule_actual_reward or 0.0
    rule_error = record.rule_prediction_error() or 0.0
    actual_vs_model = record.rule_actual_vs_model_prediction() or 0.0
    mae = "n/a" if statistics.rule_prediction_mae is None else f"{statistics.rule_prediction_mae:.3f}"
    advantage = (
        "n/a"
        if statistics.average_claimed_advantage is None
        else f"{statistics.average_claimed_advantage:+.3f}"
    )

    return "\n".join([
        "",
        "VALUE MODEL · DISAGREEMENT OUTCOME",
        DIVIDER,
        "RULE",
        f"  {record.rule_goal} → {record.rule_action}{rule_target}",
        f"  predicted   {record.rule_predicted_value:+.3f}",
        f"  actual      {rule_actual:+.3f}",
        f"  error       {rule_error:.3f}",
        "",
        "MODEL",
        f"  {record.model_goal} → {record.model_action}{model_target}",
        f"  predicted   {record.model_predicted_value:+.3f}",
        "",
        "MODEL CLAIMED ADVANTAGE",
        f"  {record.model_claimed_advantage():+.3f}",
        "",
        "RULE ACTUAL VS MODEL PREDICTION",
        f"  {actual_vs_model:+.3f}",
        DIVIDER,
        f"Disagreements: {statistics.count}",
        f"Rule prediction MAE: {mae}",
        f"Average claimed advantage: {advantage}",
        (
            "Rule actual > model predicted alternative: "
            f"{statistics.rule_actual_exceeds_model_prediction} / {statistics.count}"
        ),
        (
            "Model predicted alternative > rule actual: "
            f"{statistics.model_prediction_exceeds_rule_actual} / {statistics.count}"
        ),
        DIVIDER,
    ])


class LiveValueReporter:
    def __init__(
            self,
            *,
            output: TextIO | None = None,
            summary_interval: int = 100,
    ) -> None:
        if summary_interval <= 0:
            raise ValueError("Summary interval must be positive.")

        self.output = sys.stderr if output is None else output
        self.summary_interval = summary_interval
        self.diagnostics = RunningValueDiagnostics()
        self.completed_move_to = CompletedMoveToDiagnostics()

    def record_completed(
            self,
            experience: Experience,
            *,
            prediction: float | None,
            candidate_comparison: dict | None,
    ) -> None:
        if prediction is not None:
            self.diagnostics.record(
                action=experience.action,
                result=experience.result,
                predicted=prediction,
                actual=experience.reward,
            )
            self.completed_move_to.record(experience)

            if self.diagnostics.count % self.summary_interval == 0:
                self._print(
                    format_live_summary(
                        self.diagnostics,
                        self.completed_move_to,
                    )
                )

        if candidate_comparison is not None:
            self._print(
                format_disagreement_result(
                    candidate_comparison,
                    rule_actual=experience.reward,
                )
            )

    def report_disagreement(
            self,
            scored_candidates: list[ScoredCandidate],
            *,
            rule_key: tuple,
            model_key: tuple,
    ) -> None:
        self._print(
            format_candidate_disagreement(
                scored_candidates,
                rule_key=rule_key,
                model_key=model_key,
            )
        )

    def report_disagreement_outcome(
            self,
            record: DisagreementRecord,
            statistics: DisagreementStatistics,
    ) -> None:
        self._print(
            format_disagreement_outcome(
                record,
                statistics,
            )
        )

    def report_candidates(
            self,
            scored_candidates: list[ScoredCandidate],
            *,
            eligible_keys: set[tuple],
    ) -> None:
        self._print(
            format_candidate_scores(
                scored_candidates,
                eligible_keys=eligible_keys,
            )
        )

    def _print(self, report: str) -> None:
        print(
            report,
            file=self.output,
            flush=True,
        )
