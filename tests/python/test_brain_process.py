import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import torch

from brain.learning.features import FEATURE_NAMES
from brain.learning.model import ValueModel
from brain.learning.model_io import save_model

PROJECT_ROOT = Path(__file__).resolve().parents[2]


def preview_response(request: dict, *, path_length: int = 1) -> dict:
    return {
        "type": "preview_response",
        "previews": [
            {
                "id": candidate["id"],
                "reachable": True,
                "path_length": path_length,
                "next_step":
                    candidate["target"],
            }
            for candidate
            in request["candidates"]
        ]
    }


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


def counterfactual_response(request: dict, position: list[int]) -> dict:
    offsets = {
        "north": (0, -1),
        "east": (1, 0),
        "south": (0, 1),
        "west": (-1, 0),
    }
    results = []

    for candidate in request["candidates"]:
        decision = candidate["decision"]
        position_after = list(position)
        energy_after = 100
        outcome = None

        if decision["action"] == "move":
            offset = offsets[decision["direction"]]
            position_after = [
                position[0] + offset[0],
                position[1] + offset[1],
            ]
            energy_after = 99
        elif decision["action"] == "investigate":
            outcome = "Empty"

        results.append({
            "choice": candidate["choice"],
            "succeeded": True,
            "result": "completed",
            "position_after": position_after,
            "energy_after": energy_after,
            "path_length_before": None,
            "path_length_after": None,
            "outcome": outcome,
        })

    return {
        "type": "counterfactual_response",
        "results": results,
    }


def run_brain(working_directory: Path, observations: list[dict], preview_path_length: int = 1) -> list[dict]:
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(PROJECT_ROOT)

    process = subprocess.Popen(
        [sys.executable, "-m", "brain.main"],
        cwd=working_directory,
        env=environment,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    assert process.stdin is not None
    assert process.stdout is not None
    assert process.stderr is not None

    responses = []

    for item in observations:
        process.stdin.write(json.dumps(item) + "\n")
        process.stdin.flush()

        line = process.stdout.readline()

        if not line:
            error = process.stderr.read()

            raise AssertionError("Brain stopped before responding:\n" + error)

        response = json.loads(line)

        while response.get("type") in {
            "preview_request",
            "counterfactual_request",
        }:
            if response["type"] == "preview_request":
                reply = preview_response(
                    response,
                    path_length=preview_path_length,
                )
            else:
                reply = counterfactual_response(
                    response,
                    item["position"],
                )

            process.stdin.write(json.dumps(reply) + "\n")
            process.stdin.flush()

            line = process.stdout.readline()

            if not line:
                error = process.stderr.read()

                raise AssertionError(
                    "Brain stopped during preview:\n"
                    + error
                )

            response = json.loads(line)

        responses.append(response)

    process.stdin.close()

    return_code = process.wait(timeout=10)

    error = process.stderr.read()

    process.stdout.close()
    process.stderr.close()

    if return_code != 0:
        raise AssertionError(f"Brain exited with "f"{return_code}:\n{error}")

    return responses


class BrainProcessTests(unittest.TestCase):
    def test_counterfactual_comparison_still_executes_rule_action(self):
        world_id = "maze:counterfactual:9x9:b0:u1"
        first = observation(world_id, [1, 1])
        first["east"] = "Unknown"
        first["south"] = "Empty"
        first["visible_cells"].append({
            "position": [1, 2],
            "type": "Empty",
        })
        first["nearby_objects"] = [{
            "type": "Unknown",
            "position": [2, 1],
            "reachable": True,
            "path_length": 1,
        }]

        with tempfile.TemporaryDirectory() as directory:
            working_directory = Path(directory)
            model = ValueModel()

            with torch.no_grad():
                for parameter in model.parameters():
                    parameter.zero_()

                action_move = FEATURE_NAMES.index("action_move")
                action_investigate = FEATURE_NAMES.index(
                    "action_investigate"
                )
                model.network[0].weight[0, action_move] = 1.0
                model.network[0].weight[0, action_investigate] = -1.0
                model.network[2].weight[0, 0] = 1.0

            save_model(
                model,
                working_directory / "data" / "models" / "value_model_2.pt",
            )

            responses = run_brain(working_directory, [first])

        self.assertEqual(responses[0]["action"], "investigate")
        self.assertEqual(responses[0]["target"], [2, 1])

    def test_memory_survives_brain_restart(self):
        world_id = "maze:release-restart:9x9:b1:u1"

        with tempfile.TemporaryDirectory() as directory:
            working_directory = Path(directory)
            run_brain(working_directory, [observation(world_id, [1, 1])])
            run_brain(working_directory, [observation(world_id, [1, 1])])

            saved = json.loads(
                (working_directory / "data" / "world_memory" / "maze_release-restart_9x9_b1_u1.json").read_text()
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
                (working_directory / "data" / "world_memory" / "maze_release-seed-a_9x9_b1_u1.json").read_text()
            )
            second = json.loads(
                (working_directory / "data" / "world_memory" / "maze_release-seed-b_9x9_b1_u1.json").read_text()
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

    def test_unsafe_frontier_uses_local_move(self):
        world_id = (
            "maze:safety-integration:"
            "9x9:b1:u0"
        )

        first = observation(
            world_id,
            [1, 1],
        )

        first["energy"] = 100
        first["sensor_radius"] = 10
        first["nearby_objects"] = [
            {
                "type": "Battery",
                "position": [5, 1],
                "reachable": True,
                "path_length": 5,
            },
        ]

        second = observation(
            world_id,
            [2, 1],
        )

        second["energy"] = 99
        second["last_action"] = {
            "type": "move",
            "succeeded": True,
            "result": "completed",
        }

        with tempfile.TemporaryDirectory() as directory:
            working_directory = Path(directory)

            responses = run_brain(
                working_directory,
                [
                    first,
                    second,
                ],
                preview_path_length=49,
            )

            first_response = responses[0]

            self.assertEqual(
                first_response["action"],
                "move",
            )

            self.assertEqual(
                first_response["direction"],
                "east",
            )

            self.assertIsNone(
                first_response["debug"]["plan"]
            )

            self.assertEqual(
                first_response["debug"]
                ["navigation_safety"]
                ["reason"],
                "insufficient_round_trip_energy",
            )

            experience_files = list(
                (
                        working_directory
                        / "data"
                        / "experiences"
                ).glob("*.jsonl")
            )

            self.assertEqual(
                len(experience_files),
                1,
            )

            experiences = [
                json.loads(line)
                for line
                in experience_files[0]
                .read_text()
                .splitlines()
                if line
            ]

            self.assertEqual(
                len(experiences),
                1,
            )

            # The rejected move_to must not leak
            # into the completed experience.
            self.assertEqual(
                experiences[0]["action"],
                "move",
            )

            self.assertEqual(
                experiences[0]["position_before"],
                [1, 1],
            )

            self.assertEqual(
                experiences[0]["position_after"],
                [2, 1],
            )

    def test_navigation_preview_is_saved_in_experience(self):
        world_id = (
            "maze:preview-experience:"
            "9x9:b1:u0"
        )

        first = observation(
            world_id,
            [1, 1],
        )

        first["sensor_radius"] = 10
        first["nearby_objects"] = [
            {
                "type": "Battery",
                "position": [5, 1],
                "reachable": True,
                "path_length": 5,
            },
        ]

        second = observation(
            world_id,
            [2, 1],
        )

        second["energy"] = 99
        second["last_action"] = {
            "type": "move_to",
            "target": [2, 1],
            "succeeded": True,
            "result": "completed",
            "reachable_before": False,
            "path_length_before": 99,
            "path_length_after": 0,
        }

        with tempfile.TemporaryDirectory() as directory:
            working_directory = Path(directory)

            responses = run_brain(
                working_directory,
                [first, second],
                preview_path_length=1,
            )

            self.assertEqual(
                responses[0]["action"],
                "move_to",
            )

            experience_files = list(
                (
                    working_directory
                    / "data"
                    / "experiences"
                ).glob("*.jsonl")
            )

            self.assertEqual(
                len(experience_files),
                1,
            )

            experiences = [
                json.loads(line)
                for line
                in experience_files[0]
                .read_text()
                .splitlines()
                if line
            ]

            action_experiences = [
                experience
                for experience in experiences
                if experience["kind"] == "action"
                and experience["action"] == "move_to"
            ]

            self.assertEqual(
                len(action_experiences),
                1,
            )

            experience = action_experiences[0]

            self.assertEqual(
                experience["action"],
                "move_to",
            )

            self.assertTrue(
                experience["reachable_before"]
            )

            self.assertEqual(
                experience["path_length_before"],
                1,
            )

            self.assertFalse(
                experience[
                    "next_step_was_visited"
                ]
            )


if __name__ == "__main__":
    unittest.main()
