import json
import tempfile
import unittest
from pathlib import Path

from brain.experience import Experience
from brain.experience_store import append_experience, experience_path_for_world


def sample_experience(step: int) -> Experience:
    return Experience(
        step=step,
        goal="recharge",
        action="move_to",
        target=(5, 2),
        position_before=(2, 2),
        position_after=(3, 2),
        energy_before=90,
        energy_after=89,
        succeeded=True,
        reward=0.09,
    )


class ExperienceStoreTests(unittest.TestCase):
    def test_builds_experience_path_for_world(self):
        path = experience_path_for_world(
            Path("data"),
            "maze:1337:42x21:b12:u20",
        )

        self.assertEqual(
            path,
            Path("data/experiences/maze_1337_42x21_b12_u20.jsonl"),
        )

    def test_append_writes_json_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "experiences" / "world.jsonl"

            append_experience(sample_experience(12), path)

            data = json.loads(path.read_text().strip())
            self.assertEqual(data["step"], 12)
            self.assertEqual(data["target"], [5, 2])
            self.assertEqual(data["position_before"], [2, 2])
            self.assertEqual(data["position_after"], [3, 2])
            self.assertEqual(data["reward"], 0.09)

    def test_append_preserves_existing_experiences(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "experiences" / "world.jsonl"

            append_experience(sample_experience(12), path)
            append_experience(sample_experience(13), path)

            lines = path.read_text().splitlines()
            self.assertEqual(len(lines), 2)
            self.assertEqual(json.loads(lines[0])["step"], 12)
            self.assertEqual(json.loads(lines[1])["step"], 13)


if __name__ == "__main__":
    unittest.main()
