import unittest

import torch

from brain.learning.candidates import (
    CandidateDecision,
    ScoredCandidate,
    candidate_decisions,
    decision_key,
    score_candidates,
    select_model_candidate,
    value_input_for_candidate,
)
from brain.learning.model import ValueModel
from brain.memory import Memory


def observation_with_adjacent_unknown() -> dict:
    return {
        "position": [2, 2],
        "energy": 80,
        "north": "Unknown",
        "east": "Empty",
        "south": "Battery",
        "west": "Wall",
        "nearby_objects": [
            {
                "type": "Unknown",
                "position": [2, 1],
                "reachable": True,
                "path_length": 1,
                "next_step": [2, 1],
            },
        ],
    }


class LearningCandidateTests(unittest.TestCase):
    def test_candidates_include_rule_legal_moves_and_adjacent_investigation(self):
        memory = Memory()
        rule_action = {
            "action": "move",
            "direction": "east",
        }

        candidates = candidate_decisions(
            observation_with_adjacent_unknown(),
            memory,
            rule_goal="explore",
            rule_action=rule_action,
            recharge_urgent=False,
            plan_was_active=False,
        )
        keys = {
            decision_key(candidate.goal, candidate.action)
            for candidate in candidates
        }

        self.assertIn(
            ("explore", "move", "east", None),
            keys,
        )
        self.assertIn(
            ("explore", "move", "south", None),
            keys,
        )
        self.assertIn(
            ("investigate", "investigate", None, (2, 1)),
            keys,
        )
        self.assertNotIn(
            ("explore", "move", "north", None),
            keys,
        )

    def test_urgent_recharge_exposes_only_rule_choice(self):
        candidates = candidate_decisions(
            observation_with_adjacent_unknown(),
            Memory(),
            rule_goal="recharge",
            rule_action={
                "action": "move_to",
                "target": [5, 2],
            },
            recharge_urgent=True,
            plan_was_active=False,
        )

        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates[0].goal, "recharge")

    def test_active_plan_exposes_only_committed_rule_choice(self):
        candidates = candidate_decisions(
            observation_with_adjacent_unknown(),
            Memory(),
            rule_goal="investigate",
            rule_action={
                "action": "move_to",
                "target": [4, 2],
            },
            recharge_urgent=False,
            plan_was_active=True,
        )

        self.assertEqual(len(candidates), 1)

    def test_move_candidate_uses_pre_action_visit_history(self):
        memory = Memory()
        memory.record_visit([3, 2])
        candidate = CandidateDecision(
            goal="explore",
            action={
                "action": "move",
                "direction": "east",
            },
        )

        value_input = value_input_for_candidate(
            candidate,
            observation_with_adjacent_unknown(),
            memory,
        )

        self.assertTrue(value_input.next_step_was_visited)
        self.assertIsNone(value_input.target)

    def test_visible_move_to_candidate_uses_known_route_facts(self):
        memory = Memory()
        target = (5, 2)
        memory.remember_battery(target)
        memory.record_visit([3, 2])
        observation = observation_with_adjacent_unknown()
        observation["nearby_objects"].append({
            "type": "Battery",
            "position": list(target),
            "reachable": True,
            "path_length": 3,
            "next_step": [3, 2],
        })
        candidate = CandidateDecision(
            goal="recharge",
            action={
                "action": "move_to",
                "target": list(target),
            },
        )

        value_input = value_input_for_candidate(
            candidate,
            observation,
            memory,
        )

        self.assertEqual(value_input.path_length, 3)
        self.assertTrue(value_input.next_step_was_visited)
        self.assertEqual(
            value_input.memory_trust,
            memory.battery_trust(target),
        )

    def test_scoring_returns_one_prediction_per_candidate(self):
        torch.manual_seed(1)
        candidates = candidate_decisions(
            observation_with_adjacent_unknown(),
            Memory(),
            rule_goal="explore",
            rule_action={
                "action": "move",
                "direction": "east",
            },
            recharge_urgent=False,
            plan_was_active=False,
        )

        scored = score_candidates(
            ValueModel(),
            candidates,
            observation_with_adjacent_unknown(),
            Memory(),
        )

        self.assertEqual(len(scored), len(candidates))
        self.assertTrue(
            all(
                isinstance(item.predicted_reward, float)
                for item in scored
            )
        )

    def test_equal_predictions_prefer_rule_choice(self):
        rule = CandidateDecision(
            goal="explore",
            action={"action": "move", "direction": "east"},
        )
        alternative = CandidateDecision(
            goal="explore",
            action={"action": "move", "direction": "north"},
        )
        scored = [
            ScoredCandidate(rule, 0.1),
            ScoredCandidate(alternative, 0.1),
        ]

        selected = select_model_candidate(
            scored,
            rule_goal=rule.goal,
            rule_action=rule.action,
        )

        self.assertIs(selected.candidate, rule)

    def test_higher_prediction_selects_model_alternative(self):
        rule = CandidateDecision(
            goal="explore",
            action={"action": "move", "direction": "east"},
        )
        alternative = CandidateDecision(
            goal="investigate",
            action={"action": "investigate", "target": [2, 1]},
        )
        scored = [
            ScoredCandidate(rule, 0.1),
            ScoredCandidate(alternative, 0.4),
        ]

        selected = select_model_candidate(
            scored,
            rule_goal=rule.goal,
            rule_action=rule.action,
        )

        self.assertIs(selected.candidate, alternative)


if __name__ == "__main__":
    unittest.main()
