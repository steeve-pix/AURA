import unittest

from brain.learning.candidates import CandidateDecision
from brain.navigation_preview import (
    build_navigation_preview_request,
    navigation_previews_by_target,
    validate_navigation_preview_response,
)


class NavigationPreviewTests(unittest.TestCase):
    def test_request_assigns_ids_only_to_move_to_candidates(self):
        request = build_navigation_preview_request([
            CandidateDecision(
                goal="explore",
                action={"action": "move", "direction": "east"},
            ),
            CandidateDecision(
                goal="recharge",
                action={"action": "move_to", "target": [20, 9]},
            ),
            CandidateDecision(
                goal="investigate",
                action={"action": "move_to", "target": [11, 4]},
            ),
        ])

        self.assertEqual(request, {
            "type": "preview_request",
            "candidates": [
                {"id": 1, "action": "move_to", "target": [20, 9]},
                {"id": 2, "action": "move_to", "target": [11, 4]},
            ],
        })

    def test_request_is_absent_without_move_to_candidates(self):
        request = build_navigation_preview_request([
            CandidateDecision(
                goal="explore",
                action={"action": "move", "direction": "east"},
            ),
        ])

        self.assertIsNone(request)

    def test_response_ids_must_match_request(self):
        request = {
            "type": "preview_request",
            "candidates": [
                {"id": 1, "action": "move_to", "target": [20, 9]},
            ],
        }

        with self.assertRaises(ValueError):
            validate_navigation_preview_response(
                request,
                {
                    "type": "preview_response",
                    "previews": [{
                        "id": 2,
                        "reachable": True,
                        "path_length": 8,
                        "next_step": [6, 5],
                    }],
                },
            )

    def test_reachable_and_unreachable_responses_are_valid(self):
        request = {
            "type": "preview_request",
            "candidates": [
                {"id": 1, "action": "move_to", "target": [20, 9]},
                {"id": 2, "action": "move_to", "target": [11, 4]},
            ],
        }
        response = {
            "type": "preview_response",
            "previews": [
                {
                    "id": 1,
                    "reachable": True,
                    "path_length": 8,
                    "next_step": [6, 5],
                },
                {
                    "id": 2,
                    "reachable": False,
                    "path_length": None,
                    "next_step": None,
                },
            ],
        }

        validate_navigation_preview_response(request, response)

        previews = navigation_previews_by_target(request, response)
        self.assertEqual(previews[(20, 9)]["id"], 1)
        self.assertEqual(previews[(11, 4)]["id"], 2)


if __name__ == "__main__":
    unittest.main()
