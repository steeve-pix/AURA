import unittest

import torch

from brain.experience import Experience
from brain.learning.diagnostics import calculate_action_diagnostics, RunningValueDiagnostics


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
        "reward": 0.1,
    }
    values.update(overrides)
    return Experience(**values)


class LearningDiagnosticsTests(unittest.TestCase):
    def test_calculates_statistics_for_each_action(self):
        experiences = [
            make_experience(reward=0.1),
            make_experience(reward=0.2),
            make_experience(
                action="investigate",
                event="investigate",
                reward=1.1,
            ),
        ]
        predictions = torch.tensor([[0.2], [0.4], [0.5]])

        diagnostics = calculate_action_diagnostics(
            experiences,
            predictions,
        )

        move = diagnostics["move"]
        self.assertEqual(move.count, 2)
        self.assertAlmostEqual(move.average_actual_reward, 0.15)
        self.assertAlmostEqual(move.average_predicted_reward, 0.3)
        self.assertAlmostEqual(move.mse, 0.025)

        investigation = diagnostics["investigate"]
        self.assertEqual(investigation.count, 1)
        self.assertAlmostEqual(investigation.mse, 0.36)

    def test_rejects_misaligned_experiences_and_predictions(self):
        with self.assertRaises(ValueError):
            calculate_action_diagnostics(
                [make_experience()],
                torch.tensor([[0.1], [0.2]]),
            )

    def test_running_diagnostics_group_errors_by_action(self):
        diagnostics = RunningValueDiagnostics()

        diagnostics.record(
            action="move",
            predicted=0.2,
            actual=0.1,
        )

        diagnostics.record(
            action="move",
            predicted=0.4,
            actual=0.2,
        )

        diagnostics.record(
            action="investigate",
            predicted=0.5,
            actual=1.0,
        )

        self.assertEqual(diagnostics.count, 3)

        self.assertEqual(diagnostics.by_action["move"].count, 2)

        self.assertEqual(diagnostics.by_action["investigate"].count, 1)


if __name__ == "__main__":
    unittest.main()
