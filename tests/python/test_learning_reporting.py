import unittest
from io import StringIO

from brain.experience import Experience
from brain.learning.candidates import CandidateDecision, ScoredCandidate
from brain.learning.diagnostics import (
    CompletedMoveToDiagnostics,
    RunningValueDiagnostics,
)
from brain.learning.features import ValueInput
from brain.learning.reporting import (
    LiveValueReporter,
    format_candidate_disagreement,
    format_disagreement_result,
    format_live_summary,
)


def make_experience(**overrides) -> Experience:
    values = {
        "kind": "action",
        "event": "move",
        "step": 1,
        "goal": "explore",
        "action": "move",
        "target": None,
        "position_before": (1, 1),
        "position_after": (2, 1),
        "energy_before": 40,
        "energy_after": 39,
        "succeeded": True,
        "result": "completed",
        "reward": 0.09,
    }
    values.update(overrides)
    return Experience(**values)


class LearningReportingTests(unittest.TestCase):
    def test_live_summary_contains_aligned_metrics(self):
        diagnostics = RunningValueDiagnostics()
        diagnostics.record(
            action="move_to",
            result="completed",
            predicted=0.20,
            actual=0.24,
        )

        report = format_live_summary(
            diagnostics,
            CompletedMoveToDiagnostics(),
        )

        self.assertIn("Value Model · Live Accuracy", report)
        self.assertIn("Samples: 1", report)
        self.assertIn("Action", report)
        self.assertIn("Predicted", report)
        self.assertIn("move_to", report)
        self.assertNotIn("Value Model · Result Detail", report)

    def test_live_summary_adds_result_detail_after_a_failure(self):
        diagnostics = RunningValueDiagnostics()
        diagnostics.record(
            action="move_to",
            result="completed",
            predicted=0.20,
            actual=0.24,
        )
        diagnostics.record(
            action="move_to",
            result="unreachable",
            predicted=0.10,
            actual=-0.50,
        )

        report = format_live_summary(
            diagnostics,
            CompletedMoveToDiagnostics(),
        )

        self.assertIn("Value Model · Result Detail", report)
        self.assertIn("completed", report)
        self.assertIn("unreachable", report)

    def test_candidate_report_marks_rule_and_model(self):
        rule = ScoredCandidate(
            CandidateDecision(
                goal="explore",
                action={"action": "move", "direction": "north"},
            ),
            predicted_reward=0.09,
        )
        model = ScoredCandidate(
            CandidateDecision(
                goal="investigate",
                action={"action": "investigate", "target": [4, 7]},
            ),
            predicted_reward=0.40,
            value_input=ValueInput(
                energy=80,
                goal="investigate",
                action="investigate",
                target=(4, 7),
                position=(4, 6),
                path_length=None,
                memory_trust=None,
                next_step_was_visited=None,
            ),
        )

        report = format_candidate_disagreement(
            [rule, model],
            rule_key=("explore", "move", "north", None),
            model_key=("investigate", "investigate", None, (4, 7)),
        )

        self.assertIn("RULE", report)
        self.assertIn("MODEL", report)
        self.assertIn("explore → move north", report)
        self.assertIn("investigate → investigate (4, 7)", report)
        self.assertIn("REACHABLE", report)
        self.assertIn("NEXT VISITED", report)
        self.assertIn("advisory only", report)

    def test_result_report_explains_unexecuted_model_actual(self):
        report = format_disagreement_result(
            {
                "rule": ("explore", "move", "north", None),
                "rule_prediction": 0.09,
                "model": ("investigate", "investigate", None, (4, 7)),
                "model_prediction": 0.40,
            },
            rule_actual=0.19,
        )

        self.assertIn("error 0.100", report)
        self.assertIn("actual n/a (not executed)", report)

    def test_live_reporter_prints_summary_at_interval(self):
        output = StringIO()
        reporter = LiveValueReporter(
            output=output,
            summary_interval=2,
        )
        experience = make_experience()

        reporter.record_completed(
            experience,
            prediction=0.10,
            candidate_comparison=None,
        )
        self.assertEqual(output.getvalue(), "")

        reporter.record_completed(
            experience,
            prediction=0.10,
            candidate_comparison=None,
        )

        self.assertIn("Value Model · Live Accuracy", output.getvalue())
        self.assertIn("Samples: 2", output.getvalue())

    def test_live_reporter_prints_disagreement_result(self):
        output = StringIO()
        reporter = LiveValueReporter(output=output)

        reporter.record_completed(
            make_experience(reward=0.19),
            prediction=None,
            candidate_comparison={
                "rule": ("explore", "move", "north", None),
                "rule_prediction": 0.09,
                "model": ("investigate", "investigate", None, (4, 7)),
                "model_prediction": 0.40,
            },
        )

        self.assertIn("DISAGREEMENT RESULT", output.getvalue())
        self.assertIn("actual +0.190", output.getvalue())


if __name__ == "__main__":
    unittest.main()
