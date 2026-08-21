import tempfile
import unittest
from pathlib import Path

from brain.experience import Experience
from brain.learning.experiment import (
    experience_paths_for_seeds,
    validate_training_data,
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


class LearningExperimentTests(unittest.TestCase):
    def test_seed_files_are_resolved_individually(self):
        with tempfile.TemporaryDirectory() as directory:
            data_directory = Path(directory)
            seed_a = data_directory / "maze_2001_config.jsonl"
            seed_b = data_directory / "maze_2002_config.jsonl"
            seed_a.touch()
            seed_b.touch()

            paths = experience_paths_for_seeds(
                data_directory,
                [2001, 2002],
            )

            self.assertEqual(paths, [seed_a, seed_b])

    def test_training_thresholds_count_only_action_experiences(self):
        experiences = [
            make_experience(
                action="investigate",
                event="investigate",
                succeeded=False,
                result="failed",
            ),
            make_experience(
                kind="plan",
                action="",
                event="plan_failed",
                succeeded=False,
                result="plan_failed",
            ),
        ]

        counts = validate_training_data(
            experiences,
            minimum_failures=1,
            minimum_investigations=1,
        )

        self.assertEqual(counts, (1, 1, 1))

    def test_training_thresholds_reject_insufficient_data(self):
        with self.assertRaises(ValueError):
            validate_training_data(
                [make_experience()],
                minimum_failures=1,
                minimum_investigations=1,
            )


if __name__ == "__main__":
    unittest.main()
