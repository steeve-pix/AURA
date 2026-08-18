import unittest
from contextlib import redirect_stdout
from io import StringIO

from brain.experience import Experience
from brain.experience_analysis import (
    average_navigation_progress,
    body_action_failure_count,
    experience_kind_distribution,
    movement_counts,
    result_distribution,
    reward_distribution,
    plan_event_distribution,
    print_table,
    navigation_progress_counts,
)


def experience(
        *,
        action: str = "move",
        result: str = "completed",
        visited_new_cell: bool = False,
        navigation_progress: int | None = None,
        reward: float = 0.09,
        kind: str = "action",
        event: str | None = None,
) -> Experience:
    return Experience(
        step=1,
        kind=kind,
        event=action if event is None else event,
        goal="explore",
        action=action,
        target=None,
        position_before=(1, 1),
        position_after=(2, 1),
        energy_before=100,
        energy_after=99,
        succeeded=result == "completed",
        result=result,
        visited_new_cell=visited_new_cell,
        navigation_progress=navigation_progress,
        reward=reward,
    )


class ExperienceAnalysisTests(unittest.TestCase):
    def test_movement_counts_new_and_revisited_cells(self):
        experiences = [
            experience(visited_new_cell=True),
            experience(action="move_to"),
            experience(action="investigate", visited_new_cell=True),
        ]

        self.assertEqual(movement_counts(experiences), (1, 1))

    def test_navigation_progress_counts_positive_zero_and_negative(self):
        experiences = [
            experience(navigation_progress=2),
            experience(navigation_progress=0),
            experience(navigation_progress=-1),
            experience(navigation_progress=None),
        ]

        self.assertEqual(navigation_progress_counts(experiences), (1, 1, 1))

    def test_average_navigation_progress_uses_only_navigation_samples(self):
        experiences = [
            experience(navigation_progress=2),
            experience(navigation_progress=0),
            experience(navigation_progress=-1),
            experience(navigation_progress=None),
        ]

        self.assertAlmostEqual(
            average_navigation_progress(experiences),
            1 / 3,
        )

    def test_average_navigation_progress_is_none_without_navigation_samples(self):
        self.assertIsNone(
            average_navigation_progress([
                experience(navigation_progress=None),
            ])
        )

    def test_print_table_aligns_labels_and_values(self):
        output = StringIO()

        with redirect_stdout(output):
            print_table(
                "Summary",
                [("Experiences", 8910), ("Success rate", "99.93%")],
            )

        rendered = output.getvalue()

        self.assertIn("Summary", rendered)
        self.assertIn("| Experiences  |   8910 |", rendered)
        self.assertIn("| Success rate | 99.93% |", rendered)

    def test_result_distribution_counts_each_result(self):
        experiences = [
            experience(result="completed"),
            experience(result="failed"),
            experience(result="unreachable"),
        ]

        self.assertEqual(result_distribution(experiences)["completed"], 1)
        self.assertEqual(result_distribution(experiences)["failed"], 1)
        self.assertEqual(result_distribution(experiences)["unreachable"], 1)

    def test_reward_distribution_rounds_to_two_decimals(self):
        experiences = [
            experience(reward=0.09000000000000001),
            experience(reward=0.09),
            experience(reward=-0.25),
        ]

        self.assertEqual(reward_distribution(experiences)[0.09], 2)
        self.assertEqual(reward_distribution(experiences)[-0.25], 1)

    def test_body_action_failure_count_uses_succeeded_flag(self):
        experiences = [
            experience(result="completed"),
            experience(result="failed"),
            experience(result="unreachable"),
        ]

        self.assertEqual(body_action_failure_count(experiences), 2)

    def test_plan_failure_is_not_counted_as_body_action_failure(self):
        experiences = [
            experience(
                kind="plan",
                event="plan_failed",
                result="plan_failed",
            ),
        ]

        self.assertEqual(body_action_failure_count(experiences), 0)

    def test_plan_events_are_separate_from_action_experiences(self):
        experiences = [
            experience(),
            experience(kind="plan", event="plan_completed"),
            experience(kind="plan", event="plan_failed", result="plan_failed"),
            experience(kind="plan", event="replan"),
        ]

        self.assertEqual(experience_kind_distribution(experiences), {
            "action": 1,
            "plan": 3,
        })
        self.assertEqual(plan_event_distribution(experiences), {
            "plan_completed": 1,
            "plan_failed": 1,
            "replan": 1,
        })


if __name__ == "__main__":
    unittest.main()
