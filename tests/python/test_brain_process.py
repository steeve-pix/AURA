import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def observation(world_id: str, position: list[int]) -> dict:
    return {
        "world_id": world_id,
        "position": position,
        "energy": 100,
        "sensor_radius": 1,
        "north": "Wall",
        "east": "Empty",
        "south": "Wall",
        "west": "Wall",
        "visible_cells": [
            {"position": position, "type": "Empty"},
            {"position": [position[0] + 1, position[1]], "type": "Empty"},
        ],
        "nearby_objects": [],
        "last_action": None,
    }


def run_brain(working_directory: Path, observations: list[dict]) -> list[dict]:
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(PROJECT_ROOT)

    process = subprocess.run(
        [sys.executable, "-m", "brain.main"],
        cwd=working_directory,
        env=environment,
        input="".join(json.dumps(item) + "\n" for item in observations),
        text=True,
        capture_output=True,
        check=True,
    )

    return [json.loads(line) for line in process.stdout.splitlines()]


class BrainProcessTests(unittest.TestCase):
    def test_memory_survives_brain_restart(self):
        world_id = "maze:release-restart:9x9:b1:u1"

        with tempfile.TemporaryDirectory() as directory:
            working_directory = Path(directory)
            run_brain(working_directory, [observation(world_id, [1, 1])])
            run_brain(working_directory, [observation(world_id, [1, 1])])

            saved = json.loads(
                (working_directory / "data" / "maze_release-restart_9x9_b1_u1.json").read_text()
            )
            visits = {
                tuple(item["position"]): item["count"]
                for item in saved["visit_counts"]
            }

            self.assertEqual(saved["step"], 2)
            self.assertEqual(visits[(1, 1)], 2)

    def test_different_world_ids_use_separate_memory_in_one_process(self):
        first_world = "maze:release-seed-a:9x9:b1:u1"
        second_world = "maze:release-seed-b:9x9:b1:u1"

        with tempfile.TemporaryDirectory() as directory:
            working_directory = Path(directory)
            responses = run_brain(working_directory, [
                observation(first_world, [1, 1]),
                observation(second_world, [7, 7]),
                observation(first_world, [1, 1]),
            ])

            first = json.loads(
                (working_directory / "data" / "maze_release-seed-a_9x9_b1_u1.json").read_text()
            )
            second = json.loads(
                (working_directory / "data" / "maze_release-seed-b_9x9_b1_u1.json").read_text()
            )

            self.assertEqual(len(responses), 3)
            self.assertEqual(first["step"], 2)
            self.assertEqual(second["step"], 1)
            self.assertEqual(first["world_id"], first_world)
            self.assertEqual(second["world_id"], second_world)
            self.assertEqual(responses[0]["debug"]["failures"], {
                "plan_failures": 0,
                "replans": 0,
                "failed_targets": 0,
                "body_action_failures": 0,
            })


if __name__ == "__main__":
    unittest.main()
