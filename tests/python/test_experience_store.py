import json
import tempfile
import unittest
from pathlib import Path

from brain.experience import Experience
from brain.experience_analysis import load_experiences
from brain.experience_store import append_experience, experience_path_for_world


def sample_experience(step: int) -> Experience:
    return Experience(
        step=step,
        kind="action",
        event="move_to",
        goal="recharge",
        action="move_to",
        target=(5, 2),
        position_before=(2, 2),
        position_after=(3, 2),
        energy_before=90,
        energy_after=89,
        succeeded=True,
        result="completed",
        navigation_progress=1,
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
            self.assertEqual(data["kind"], "action")
            self.assertEqual(data["event"], "move_to")
            self.assertEqual(data["target"], [5, 2])
            self.assertEqual(data["position_before"], [2, 2])
            self.assertEqual(data["position_after"], [3, 2])
            self.assertEqual(data["reward"], 0.09)
            self.assertEqual(data["result"], "completed")
            self.assertFalse(data["visited_new_cell"])
            self.assertNotIn("discovered_new_cell", data)
            self.assertEqual(data["navigation_progress"], 1)
            self.assertNotIn("progressed_toward_target", data)

            loaded = load_experiences(path)
            self.assertEqual(loaded[0].navigation_progress, 1)

    def test_append_preserves_existing_experiences(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "experiences" / "world.jsonl"

            append_experience(sample_experience(12), path)
            append_experience(sample_experience(13), path)

            lines = path.read_text().splitlines()
            self.assertEqual(len(lines), 2)
            self.assertEqual(json.loads(lines[0])["step"], 12)
            self.assertEqual(json.loads(lines[1])["step"], 13)

    def test_append_writes_plan_event_in_shared_schema(self):
        experience = Experience(
            step=14,
            kind="plan",
            event="plan_failed",
            goal="investigate",
            action="",
            target=(5, 2),
            position_before=(4, 2),
            position_after=(4, 2),
            energy_before=89,
            energy_after=89,
            succeeded=False,
            result="plan_failed",
            reward=-0.40,
        )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "experiences" / "world.jsonl"
            append_experience(experience, path)
            data = json.loads(path.read_text())

        self.assertEqual(data["kind"], "plan")
        self.assertEqual(data["event"], "plan_failed")
        self.assertEqual(data["action"], "")
        self.assertEqual(data["reward"], -0.40)


if __name__ == "__main__":
    unittest.main()
