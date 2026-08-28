import unittest

from brain.goals import GoalProposal, investigation_score, recharge_score, recharge_is_urgent, \
    investigation_goal_proposals, select_best_investigation_proposal, recharge_goal_proposal, goal_proposals, \
    exploration_goal_proposal, select_goal_proposal, propose_goal, exploration_frontiers, select_exploration_frontier
from brain.memory import Memory


class TestGoals(unittest.TestCase):
    def setUp(self):
        self.observation = {
            "energy": 80,
            "position": [4, 2],
            "nearby_objects": [],
        }

    def test_route_cost_makes_recharge_urgent(self):
        observation = dict(
            self.observation,
            energy=30,
            nearby_objects=[
                {
                    "type": "Battery",
                    "position": [12, 8],
                    "reachable": True,
                    "path_length": 26,
                },
            ],
        )

        self.assertEqual(recharge_score(observation), 1.0)

    def test_short_route_does_not_create_emergency_recharge(self):
        observation = dict(
            self.observation,
            energy=80,
            nearby_objects=[
                {
                    "type": "Battery",
                    "position": [6, 2],
                    "reachable": True,
                    "path_length": 10,
                },
            ],
        )

        self.assertAlmostEqual(recharge_score(observation), 0.2)

    def test_previous_battery_result_increases_investigation_score(self):
        observation = dict(
            self.observation,
            nearby_objects=[
                {
                    "type": "Unknown",
                    "position": [12, 5],
                    "reachable": True,
                },
            ],
        )
        novel_memory = Memory()
        historical_memory = Memory()
        historical_memory.remember_investigation_result([12, 5], "Battery")

        self.assertGreater(
            investigation_score(observation, historical_memory),
            investigation_score(observation, novel_memory),
        )

    def test_unreachable_historical_battery_result_adds_no_urgency(self):
        observation = dict(
            self.observation,
            nearby_objects=[
                {
                    "type": "Unknown",
                    "position": [12, 5],
                    "reachable": False,
                },
            ],
        )
        memory = Memory()
        memory.remember_investigation_result([12, 5], "Battery")

        self.assertEqual(investigation_score(observation, memory), 0.0)

    def test_low_energy_makes_recharge_urgent(self):
        observation = {
            "energy": 30,
            "position": [1, 1],
            "nearby_objects": []
        }

        self.assertTrue(recharge_is_urgent(observation))

    def test_high_energy_is_not_urgent(self):
        observation = {
            "energy": 80,
            "position": [1, 1],
            "nearby_objects": []
        }

        self.assertFalse(recharge_is_urgent(observation))

    def test_goal_proposal_keeps_objective_context(self):
        proposal = GoalProposal(
            goal_type="investigate",
            target=(12, 5),
            score=0.78,
            urgency=0.20,
            reason="reachable_unknown"
        )

        self.assertEqual(proposal.goal_type, "investigate")
        self.assertEqual(proposal.target, (12, 5))
        self.assertEqual(proposal.score, 0.78)
        self.assertEqual(proposal.urgency, 0.20, )
        self.assertEqual(proposal.reason, "reachable_unknown")

    def test_investigation_proposals_have_targets(self):
        observation = {
            "energy": 80,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [5, 2],
                    "reachable": True,
                },
                {
                    "type": "Unknown",
                    "position": [12, 8],
                    "reachable": True,
                },
            ],
        }

        proposals = investigation_goal_proposals(observation, Memory())

        self.assertEqual(
            [proposal.target for proposal in proposals],
            [
                (5, 2),
                (12, 8)
            ])

        self.assertTrue(
            all(proposal.goal_type == "investigate" for proposal in proposals))

    def test_investigation_proposals_exclude_invalid_targets(self):
        memory = Memory()
        memory.mark_target_failed((8, 4))

        observation = {
            "energy": 80,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [5, 2],
                    "reachable": True,
                },
                {
                    "type": "Unknown",
                    "position": [7, 3],
                    "reachable": False,
                },
                {
                    "type": "Unknown",
                    "position": [8, 4],
                    "reachable": True,
                },
                {
                    "type": "Battery",
                    "position": [9, 4],
                    "reachable": True,
                },
            ],
        }

        proposals = investigation_goal_proposals(observation, memory)
        self.assertEqual(len(proposals), 1)
        self.assertEqual(proposals[0].target, (5, 2))

    def test_historical_result_improves_only_matching_proposals(self):
        memory = Memory()
        memory.remember_investigation_result((12, 8), "Battery")

        observation = {
            "energy": 80,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [5, 2],
                    "reachable": True,
                },
                {
                    "type": "Unknown",
                    "position": [12, 8],
                    "reachable": True,
                },
            ],
        }

        proposals = investigation_goal_proposals(observation, memory)

        self.assertGreater(
            proposals[1].score,
            proposals[0].score,
        )

    def test_shorter_route_produces_better_proposal(self):
        observation = {
            "energy": 80,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [3, 1],
                    "reachable": True,
                    "path_length": 2,
                },
                {
                    "type": "Unknown",
                    "position": [8, 1],
                    "reachable": True,
                    "path_length": 7,
                },
            ],
        }

        proposals = investigation_goal_proposals(observation, Memory())

        by_target = {proposal.target: proposal for proposal in proposals}

        self.assertGreater(
            by_target[(3, 1)].score,
            by_target[(8, 1)].score,
        )

    def test_proposals_prefer_bfs_cost_over_manhattan_distance(self):
        observation = {
            "energy": 80,
            "position": [1, 1],
            "nearby_objects": [
                {
                    # Geometrically close, but behind a detour.
                    "type": "Unknown",
                    "position": [3, 1],
                    "reachable": True,
                    "path_length": 9,
                },
                {
                    # Geometrically farther, but route is cheaper.
                    "type": "Unknown",
                    "position": [6, 1],
                    "reachable": True,
                    "path_length": 5,
                },
            ],
        }

        proposals = investigation_goal_proposals(observation, Memory())
        by_target = {proposal.target: proposal for proposal in proposals}

        self.assertGreater(
            by_target[(6, 1)].score,
            by_target[(3, 1)].score,
        )

    def test_selects_highest_scoring_investigation_proposal(self):
        proposals = [
            GoalProposal(
                goal_type="investigate",
                target=(5, 2),
                score=0.76,
                urgency=0.0,
                reason="reachable_unknown",
            ),
            GoalProposal(
                goal_type="investigate",
                target=(8, 4),
                score=0.84,
                urgency=0.0,
                reason="reachable_unknown",
            )
        ]

        selected = select_best_investigation_proposal(proposals)

        self.assertEqual(
            selected.target,
            (8, 4),
        )

    def test_no_investigation_proposals_selects_nothing(self):
        self.assertIsNone(
            select_best_investigation_proposal([]),
        )

    def test_recharge_proposal_contains_selected_battery(self):
        observation = {
            "energy": 30,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [8, 4],
                    "reachable": True,
                    "path_length": 9,
                },
                {
                    "type": "Battery",
                    "position": [5, 2],
                    "reachable": True,
                    "path_length": 5,
                },
            ],
        }

        proposal = recharge_goal_proposal(observation, Memory())

        self.assertIsNotNone(proposal)
        self.assertEqual(proposal.goal_type, "recharge")
        self.assertEqual(proposal.target, (5, 2))
        self.assertEqual(proposal.reason, "visible_viable_battery")

    def test_recharge_proposal_rejects_energy_infeasible_battery(self):
        observation = {
            "energy": 10,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [8, 4],
                    "reachable": True,
                    "path_length": 12,
                },
            ],
        }

        proposal = recharge_goal_proposal(observation, Memory())

        self.assertIsNone(proposal)

    def test_recharge_proposal_can_use_remembered_battery(self):
        memory = Memory()
        memory.remember_battery((5, 2))

        observation = {
            "energy": 30,
            "position": [1, 1],
            "nearby_objects": [],
        }

        proposal = recharge_goal_proposal(observation, memory)

        self.assertIsNotNone(proposal)

        self.assertEqual(proposal.target, (5, 2))
        self.assertEqual(proposal.reason, "remembered_viable_battery")

    def test_goal_proposals_include_available_objectives(self):
        observation = {
            "energy": 30,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [5, 2],
                    "reachable": True,
                    "path_length": 5,
                },
                {
                    "type": "Unknown",
                    "position": [7, 3],
                    "reachable": True,
                    "path_length": 7,
                },
            ],
        }

        proposals = goal_proposals(observation, Memory())

        proposal_types = [proposal.goal_type for proposal in proposals]

        self.assertEqual(
            proposal_types,
            [
                "recharge",
                "explore",
                "investigate",
            ],
        )

        self.assertEqual(proposals[0].target, (5, 2))
        self.assertIsNone(proposals[1].target)
        self.assertEqual(proposals[2].target, (7, 3))

    def test_critical_energy_proposes_untargeted_recharge(self):
        observation = {
            "energy": 5,
            "position": [1, 1],
            "nearby_objects": []
        }

        proposals = goal_proposals(observation, Memory())

        recharge = proposals[0]

        self.assertEqual(recharge.goal_type, "recharge")
        self.assertIsNone(recharge.target)
        self.assertEqual(recharge.reason, "no_viable_battery")
        self.assertGreater(recharge.urgency, 0.0)

    def test_exploration_proposal_uses_local_fallback_without_frontier(self):
        proposal = exploration_goal_proposal({
            "energy": 80,
            "position": [2, 2],
            "nearby_objects": []
        }, Memory())

        self.assertEqual(proposal.goal_type, "explore")
        self.assertIsNone(proposal.target)
        self.assertEqual(proposal.reason, "local_exploration")

    def test_selector_chooses_highest_score_without_current_goal(self):
        proposals = [
            GoalProposal(
                "explore",
                None,
                0.40,
                0.0,
                "exploration",
            ),
            GoalProposal(
                "investigate",
                (5, 2),
                0.80,
                0.0,
                "unknown",
            ),
        ]

        selected = select_goal_proposal(
            proposals,
            current_goal=None,
            energy=80,
        )

        self.assertEqual(selected.goal_type, "investigate")

    def test_selector_keeps_current_goal_within_margin(self):
        proposals = [
            GoalProposal(
                "explore",
                None,
                0.60,
                0.0,
                "exploration",
            ),
            GoalProposal(
                "investigate",
                (5, 2),
                0.65,
                0.0,
                "unknown",
            ),
        ]

        selected = select_goal_proposal(
            proposals,
            current_goal="explore",
            energy=80,
        )

        self.assertEqual(selected.goal_type, "explore")

    def test_selector_keeps_recharge_until_full(self):
        proposals = [
            GoalProposal(
                "recharge",
                (4, 2),
                0.40,
                0.0,
                "battery",
            ),
            GoalProposal(
                "investigate",
                (5, 2),
                0.90,
                0.0,
                "unknown",
            ),
        ]

        selected = select_goal_proposal(
            proposals,
            current_goal="recharge",
            energy=60,
        )

        self.assertEqual(selected.goal_type, "recharge", )

    def test_propose_goal_returns_target_specific_objective(self):
        memory = Memory()

        observation = {
            "energy": 80,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [5, 2],
                    "reachable": True,
                    "path_length": 5,
                },
            ],
        }

        proposal = propose_goal(
            observation,
            memory,
        )

        self.assertEqual(proposal.goal_type, "investigate")
        self.assertEqual(proposal.target, (5, 2))

    def test_propose_goal_does_not_mutate_memory(self):
        memory = Memory()
        memory.set_active_goal("explore")

        observation = {
            "energy": 80,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Unknown",
                    "position": [5, 2],
                    "reachable": True,
                    "path_length": 5,
                },
            ],
        }

        proposal = propose_goal(
            observation,
            memory,
        )

        self.assertEqual(
            proposal.goal_type,
            "investigate",
        )

        # Reasoning selected investigate, but did not
        # commit that decision to Memory.
        self.assertEqual(
            memory.active_goal,
            "explore",
        )

    def test_propose_goal_prioritizes_energy_emergency(self):
        memory = Memory()
        memory.set_active_goal("investigate")

        observation = {
            "energy": 5,
            "position": [1, 1],
            "nearby_objects": [],
        }

        proposal = propose_goal(
            observation,
            memory,
        )

        self.assertEqual(proposal.goal_type, "recharge")
        self.assertGreater(proposal.urgency, 0.0)

        # Still unchanged because proposing is pure.
        self.assertEqual(memory.active_goal, "investigate")

    def test_selector_switches_when_challenger_clearly_wins(self):
        proposals = [
            GoalProposal(
                "explore",
                None,
                0.60,
                0.0,
                "exploration",
            ),
            GoalProposal(
                "investigate",
                (5, 2),
                0.80,
                0.0,
                "unknown",
            ),
        ]

        selected = select_goal_proposal(
            proposals,
            current_goal="explore",
            energy=80,
        )

        self.assertEqual(
            selected.goal_type,
            "investigate",
        )

    def test_selector_keeps_current_goal_when_it_is_best(self):
        proposals = [
            GoalProposal(
                "explore",
                None,
                0.70,
                0.0,
                "exploration",
            ),
            GoalProposal(
                "investigate",
                (5, 2),
                0.65,
                0.0,
                "unknown",
            ),
        ]

        selected = select_goal_proposal(
            proposals,
            current_goal="explore",
            energy=80,
        )

        self.assertEqual(
            selected.goal_type,
            "explore",
        )

    def test_known_walkable_edge_cell_is_frontier(self):
        memory = Memory()
        memory.remember_cell([1, 1], "Empty")
        memory.remember_cell([2, 1], "Empty")

        frontiers = exploration_frontiers(
            {
                "position": [1, 1],
            },
            memory,
        )

        self.assertEqual(
            frontiers,
            [(2, 1)],
        )

    def test_wall_is_not_exploration_frontier(self):
        memory = Memory()
        memory.remember_cell([1, 1], "Empty")
        memory.remember_cell([2, 1], "Wall")

        frontiers = exploration_frontiers(
            {
                "position": [1, 1],
            },
            memory,
        )

        self.assertEqual(
            frontiers,
            [],
        )

    def test_exploration_prefers_less_visited_frontier(self):
        memory = Memory()
        memory.remember_cell([1, 1], "Empty")
        memory.remember_cell([2, 1], "Empty")
        memory.remember_cell([1, 2], "Empty")

        memory.record_visit([2, 1])

        selected = select_exploration_frontier(
            {
                "position": [1, 1],
            },
            memory,
        )

        self.assertEqual(
            selected,
            (1, 2),
        )

    def test_exploration_proposal_targets_frontier(self):
        memory = Memory()
        memory.remember_cell([1, 1], "Empty")
        memory.remember_cell([2, 1], "Empty")

        proposal = exploration_goal_proposal(
            {
                "energy": 80,
                "position": [1, 1],
                "nearby_objects": [],
            },
            memory,
        )

        self.assertEqual(
            proposal.target,
            (2, 1),
        )
        self.assertEqual(
            proposal.reason,
            "frontier_exploration",
        )
if __name__ == '__main__':
    unittest.main()
