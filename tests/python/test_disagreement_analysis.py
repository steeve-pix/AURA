import unittest

from brain.learning.disagreement_analysis import DisagreementAnalysis
from brain.learning.reporting import format_disagreement_outcome


class DisagreementAnalysisTests(unittest.TestCase):
    def test_tracks_prediction_error_and_claimed_advantage(self):
        analysis = DisagreementAnalysis()
        first = analysis.begin(
            step=10,
            rule_goal="investigate",
            rule_action={"action": "move_to", "target": [11, 9]},
            rule_predicted_value=0.14,
            model_goal="explore",
            model_action={"action": "move", "direction": "east"},
            model_predicted_value=0.18,
        )
        second = analysis.begin(
            step=11,
            rule_goal="explore",
            rule_action={"action": "move", "direction": "west"},
            rule_predicted_value=0.30,
            model_goal="explore",
            model_action={"action": "move", "direction": "east"},
            model_predicted_value=0.40,
        )

        analysis.complete(first, rule_actual_reward=0.24)
        analysis.complete(second, rule_actual_reward=0.20)
        statistics = analysis.statistics()

        self.assertEqual(first.rule_target, (11, 9))
        self.assertIsNone(first.model_target)
        self.assertAlmostEqual(first.rule_prediction_error(), 0.10)
        self.assertAlmostEqual(first.model_claimed_advantage(), 0.04)
        self.assertAlmostEqual(
            first.rule_actual_vs_model_prediction(),
            0.06,
        )
        self.assertEqual(statistics.count, 2)
        self.assertAlmostEqual(statistics.rule_prediction_mae, 0.10)
        self.assertAlmostEqual(statistics.average_claimed_advantage, 0.07)
        self.assertEqual(
            statistics.rule_actual_exceeds_model_prediction,
            1,
        )
        self.assertEqual(
            statistics.model_prediction_exceeds_rule_actual,
            1,
        )

    def test_outcome_report_includes_evidence_and_statistics(self):
        analysis = DisagreementAnalysis()
        record = analysis.begin(
            step=10,
            rule_goal="investigate",
            rule_action={"action": "move_to", "target": [11, 9]},
            rule_predicted_value=0.14,
            model_goal="explore",
            model_action={"action": "move", "direction": "east"},
            model_predicted_value=0.18,
        )
        analysis.complete(record, rule_actual_reward=0.24)

        report = format_disagreement_outcome(
            record,
            analysis.statistics(),
        )

        self.assertIn("DISAGREEMENT OUTCOME", report)
        self.assertIn("error       0.100", report)
        self.assertIn("MODEL CLAIMED ADVANTAGE", report)
        self.assertIn("Disagreements: 1", report)


if __name__ == "__main__":
    unittest.main()
