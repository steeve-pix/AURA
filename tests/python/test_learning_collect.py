import unittest
from pathlib import Path

from brain.learning.collect import body_command


class LearningCollectTests(unittest.TestCase):
    def test_body_command_contains_seed_step_limit_and_challenge(self):
        command = body_command(
            Path("build/body/aura_body"),
            Path("/project/AURA"),
            2001,
            600,
        )

        self.assertEqual(command, [
            "build/body/aura_body",
            "/project/AURA",
            "--seed",
            "2001",
            "--max-steps",
            "600",
            "--challenge-scenario",
        ])


if __name__ == "__main__":
    unittest.main()
