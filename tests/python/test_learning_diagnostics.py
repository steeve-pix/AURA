import unittest

import torch

from brain.experience import Experience
from brain.learning.diagnostics import calculate_action_diagnostics, RunningValueDiagnostics, \
    calculate_action_result_rewards, calculate_completed_move_to_diagnostics, \
    calculate_move_to_visit_result_rewards


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
            result="completed",
            predicted=0.2,
            actual=0.1,
        )

        diagnostics.record(
            action="move",
            result="completed",
            predicted=0.4,
            actual=0.2,
        )

        diagnostics.record(
            action="investigate",
            result="completed",
            predicted=0.5,
            actual=1.0,
        )

        self.assertEqual(diagnostics.count, 3)

        self.assertEqual(diagnostics.by_action["move"].count, 2)

        self.assertEqual(diagnostics.by_action["investigate"].count, 1)

    def test_calculates_rewards_by_action_result(self):
        experiences = [
            make_experience(
                action="move_to",
                result="completed",
                reward=0.24
            ),
            make_experience(
                action="move_to",
                result="completed",
                reward=0.14
            ),
            make_experience(
                action="move_to",
                result="failed",
                reward=-0.25
            ),
            make_experience(
                action="move",
                result="completed",
                reward=0.19
            ),
        ]

        diagnostics = calculate_action_result_rewards(experiences, action="move_to")

        self.assertEqual(diagnostics["completed"].count, 2)
        self.assertAlmostEqual(diagnostics["completed"].average_reward, 0.19)
        self.assertEqual(diagnostics["failed"].count, 1)
        self.assertAlmostEqual(diagnostics["failed"].average_reward, -0.25)

    def test_completed_move_to_components(self):
        experiences = [
            make_experience(
                action="move_to",
                result="completed",
                reward=0.24,
                navigation_progress=1,
                visited_new_cell=True,
                energy_before=40,
                energy_after=39,
            ),
            make_experience(
                action="move_to",
                result="completed",
                reward=0.14,
                navigation_progress=1,
                visited_new_cell=False,
                energy_before=39,
                energy_after=38,
            ),
            make_experience(
                action="move_to",
                result="failed",
                reward=-0.25,
            )
        ]

        diagnostics = calculate_completed_move_to_diagnostics(experiences)

        self.assertEqual(diagnostics.count, 2)
        self.assertAlmostEqual(diagnostics.average_reward(), 0.19)
        self.assertAlmostEqual(diagnostics.average_navigation_progress(), 1.0)
        self.assertAlmostEqual(diagnostics.visited_new_cell_rate(), 0.5)
        self.assertAlmostEqual(diagnostics.average_energy_cost(), 1.0)

    def test_move_to_rewards_are_grouped_by_visit_state_and_result(self):
        experiences = [
            make_experience(
                action="move_to",
                next_step_was_visited=False,
                result="completed",
                reward=0.24,
            ),
            make_experience(
                action="move_to",
                next_step_was_visited=False,
                result="completed",
                reward=0.14,
            ),
            make_experience(
                action="move_to",
                next_step_was_visited=False,
                result="unreachable",
                reward=-0.50,
            ),
            make_experience(
                action="move_to",
                next_step_was_visited=True,
                result="completed",
                reward=0.14,
            ),
            make_experience(
                action="move_to",
                next_step_was_visited=True,
                result="unreachable",
                reward=-0.50,
            ),
            make_experience(
                action="move",
                next_step_was_visited=False,
                result="completed",
                reward=0.19,
            ),
        ]

        diagnostics = calculate_move_to_visit_result_rewards(
            experiences
        )

        new_completed = diagnostics[(False, "completed")]
        self.assertEqual(new_completed.count, 2)
        self.assertAlmostEqual(new_completed.average_reward, 0.19)

        new_unreachable = diagnostics[(False, "unreachable")]
        self.assertEqual(new_unreachable.count, 1)
        self.assertAlmostEqual(new_unreachable.average_reward, -0.50)

        visited_completed = diagnostics[(True, "completed")]
        self.assertEqual(visited_completed.count, 1)
        self.assertAlmostEqual(visited_completed.average_reward, 0.14)

        visited_unreachable = diagnostics[(True, "unreachable")]
        self.assertEqual(visited_unreachable.count, 1)
        self.assertAlmostEqual(visited_unreachable.average_reward, -0.50)


if __name__ == "__main__":
    unittest.main()
