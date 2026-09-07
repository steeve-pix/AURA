import unittest

from brain.learning.candidates import CandidateDecision, ScoredCandidate
from brain.learning.counterfactual import (
    CounterfactualSelection,
    build_counterfactual_request,
    counterfactual_results,
    counterfactual_reward, build_single_counterfactual_request, single_counterfactual_result,
)
from brain.learning.features import ValueInput
from brain.memory import Memory


class CounterfactualTests(unittest.TestCase):
    def test_request_preserves_labeled_actions(self):
        selection = CounterfactualSelection(
            rule=ScoredCandidate(
                CandidateDecision(
                    "explore",
                    {"action": "move", "direction": "east"},
                ),
                0.1,
            ),
            model=ScoredCandidate(
                CandidateDecision(
                    "investigate",
                    {"action": "investigate", "target": [2, 1]},
                ),
                0.2,
            ),
        )

        request = build_counterfactual_request(selection)

        self.assertEqual(request["type"], "counterfactual_request")
        self.assertEqual(request["candidates"][0]["choice"], "rule")
        self.assertEqual(
            request["candidates"][1]["decision"]["target"],
            [2, 1],
        )

    def test_move_to_reward_uses_hypothetical_progress(self):
        memory = Memory()
        memory.step = 4
        memory.record_visit([1, 1])
        scored = ScoredCandidate(
            CandidateDecision(
                "explore",
                {"action": "move_to", "target": [4, 1]},
            ),
            0.2,
            value_input=ValueInput(
                energy=10,
                goal="explore",
                action="move_to",
                target=(4, 1),
                position=(1, 1),
                path_length=3,
                memory_trust=None,
                next_step_was_visited=False,
                reachable=True,
            ),
            reachable=True,
        )

        reward = counterfactual_reward(
            scored,
            {
                "choice": "rule",
                "succeeded": True,
                "result": "completed",
                "position_after": [2, 1],
                "energy_after": 9,
                "path_length_before": 3,
                "path_length_after": 2,
                "outcome": None,
            },
            {"position": [1, 1], "energy": 10},
            memory,
        )

        self.assertEqual(reward, 0.24)
        self.assertEqual(memory.visit_count((2, 1)), 0)

    def test_response_requires_both_choices(self):
        with self.assertRaises(ValueError):
            counterfactual_results({
                "type": "counterfactual_response",
                "results": [{"choice": "rule"}],
            })

    def test_single_request_contains_one_labeled_action(self):
        action = {
            "action": "move",
            "direction": "east",
        }

        request = build_single_counterfactual_request(action, choice="rule")

        self.assertEqual(request["type"], "counterfactual_request")
        self.assertEqual(
            request["candidates"],
            [
                {
                    "choice": "rule",
                    "decision": action,
                },
            ],
        )

    def test_single_request_preserves_complete_action(self):
        action = {
            "action": "move_to",
            "target": [4, 1],

        }
        request = build_single_counterfactual_request(action, choice="rule")

        self.assertEqual(
            request["candidates"][0]["decision"],
            action,
        )

    def test_single_result_returns_matching_labeled_result(self):
        expected = {
            "choice": "rule",
            "succeeded": True,
            "result": "completed",
            "position_after": [2, 1],
            "energy_after": 9,
            "observation_after": {
                "world_id": "simulation-test",
                "position": [2, 1],
                "energy": 9,
            }
        }

        result = single_counterfactual_result(
            {
                "type": "counterfactual_response",
                "results": [expected],
            },
            choice="rule",
        )

        self.assertEqual(result, expected)

    def test_single_result_rejects_wrong_choice(self):
        with self.assertRaises(ValueError):
            single_counterfactual_result(
                {
                    "type": "counterfactual_response",
                    "results": [
                        {
                            "choice": "model",
                        },
                    ],
                },
                choice="rule",
            )


if __name__ == "__main__":
    unittest.main()
