import unittest
from unittest.mock import patch

from brain import simulation_snapshot
from brain.memory import Memory
from brain.simulation_snapshot import (
    HorizonResult,
    SimulationBranch,
    accumulate_rewards,
    capture_brain_snapshot,
    choose_branch_action,
    choose_rule_action_for_branch,
    consume_forced_first_action,
    create_branch,
    restore_brain_snapshot,
    run_horizon, begin_branch_step, complete_branch_step, begin_next_branch_step,
)


def fake_step(branch: SimulationBranch) -> float:
    branch.memory.advance_step()

    return 0.1


class BrainSimulationSnapshotTests(unittest.TestCase):
    def test_horizon_result_records_reward_and_completed_steps(self):
        result = HorizonResult(
            cumulative_reward=0.45,
            steps_completed=5,
        )

        self.assertEqual(result.cumulative_reward, 0.45)
        self.assertEqual(result.steps_completed, 5)

    def test_accumulate_rewards_returns_total_and_step_count(self):
        result = accumulate_rewards([0.14, 0.09, 0.10, -0.25, 0.19])

        self.assertEqual(result.steps_completed, 5)
        self.assertEqual(result.cumulative_reward, 0.27)

    def test_restored_branches_do_not_share_mutable_memory(self):
        battery_position = (2, 1)
        memory = Memory()
        memory.remember_battery(battery_position)
        snapshot = capture_brain_snapshot(memory)

        branch_a = restore_brain_snapshot(snapshot)
        branch_b = restore_brain_snapshot(snapshot)

        branch_a.mark_battery_stale(battery_position)

        self.assertNotIn(battery_position, branch_a.batteries())
        self.assertIn(battery_position, branch_b.batteries())
        self.assertIn(battery_position, snapshot.memory.batteries())

    def test_run_horizon_accumulates_rewards_for_requested_steps(self):
        branch = create_branch(capture_brain_snapshot(Memory()))
        rewards = iter([0.14, 0.09, 0.10, -0.25, 0.19])

        result = run_horizon(
            branch=branch,
            horizon=5,
            step_fn=lambda _branch: next(rewards),
        )

        self.assertEqual(result.steps_completed, 5)
        self.assertEqual(result.cumulative_reward, 0.27)

    def test_run_horizon_stops_when_step_returns_none(self):
        branch = create_branch(capture_brain_snapshot(Memory()))
        rewards = iter([0.14, 0.09, 0.10, None])

        result = run_horizon(
            branch=branch,
            horizon=5,
            step_fn=lambda _branch: next(rewards),
        )

        self.assertEqual(result.steps_completed, 3)
        self.assertEqual(result.cumulative_reward, 0.33)

    def test_create_branch_clones_snapshot_memory(self):
        snapshot = capture_brain_snapshot(Memory())
        branch = create_branch(snapshot)

        result = run_horizon(branch=branch, horizon=3, step_fn=fake_step)

        branch_a = create_branch(snapshot)
        branch_b = create_branch(snapshot)

        self.assertIsNot(branch_a.memory, branch_b.memory)
        self.assertIsNot(branch_a.memory, snapshot.memory)

        self.assertEqual(result.steps_completed, 3)
        self.assertAlmostEqual(result.cumulative_reward, 0.30)

    def test_branches_preserve_their_own_forced_first_actions(self):
        memory = Memory()
        snapshot = capture_brain_snapshot(memory)

        rule_branch = create_branch(
            snapshot, forced_first_action={
                "action": "move",
                "direction": "east",
            })

        model_branch = create_branch(
            snapshot, forced_first_action={
                "action": "move",
                "direction": "west",
            })

        self.assertEqual(rule_branch.forced_first_action["direction"], "east")
        self.assertEqual(model_branch.forced_first_action["direction"], "west")

        self.assertIsNot(rule_branch.memory, model_branch.memory)

    def test_forced_first_action_is_consumed_only_once(self):
        memory = Memory()
        snapshot = capture_brain_snapshot(memory)

        branch = create_branch(snapshot, forced_first_action={
            "type": "move",
            "direction": "east",
        })

        first = consume_forced_first_action(branch)
        second = consume_forced_first_action(branch)

        self.assertEqual(first, {
            "type": "move",
            "direction": "east",
        })

        self.assertEqual(second, None)
        self.assertEqual(branch.forced_first_action_consumed, True)

    def test_rule_action_for_branch_uses_branch_memory(self):
        real_memory = Memory()
        snapshot = capture_brain_snapshot(real_memory)
        viable_branch = create_branch(snapshot)
        failed_target_branch = create_branch(snapshot)
        failed_target_branch.memory.mark_target_failed((2, 1))
        observation = {
            "energy": 20,
            "position": [1, 1],
            "nearby_objects": [
                {
                    "type": "Battery",
                    "position": [2, 1],
                    "reachable": True,
                    "path_length": 1,
                },
            ],
            "north": "Wall",
            "east": "Wall",
            "south": "Wall",
            "west": "Wall",
        }

        viable_action = choose_rule_action_for_branch(
            viable_branch,
            observation,
        )
        failed_target_action = choose_rule_action_for_branch(
            failed_target_branch,
            observation,
        )

        self.assertEqual(
            viable_action,
            {
                "action": "move_to",
                "target": [2, 1],
            },
        )
        self.assertEqual(failed_target_action, {"action": "idle"})
        self.assertEqual(viable_branch.memory.active_goal, "recharge")
        self.assertEqual(failed_target_branch.memory.active_goal, "recharge")
        self.assertIsNone(real_memory.active_goal)
        self.assertIsNone(real_memory.active_plan)
        self.assertIsNone(snapshot.memory.active_goal)
        self.assertIsNone(snapshot.memory.active_plan)

    def test_branch_action_uses_forced_action_once_then_rule_policy(self):
        memory = Memory()
        snapshot = capture_brain_snapshot(memory)
        branch = create_branch(snapshot, forced_first_action={
            "action": "move",
            "direction": "east",
        })
        observation = {
            "energy": 80,
            "position": [1, 1],
            "nearby_objects": [],
            "north": "Wall",
            "east": "Wall",
            "south": "Wall",
            "west": "Wall",
        }

        first = choose_branch_action(branch, observation)
        second = choose_branch_action(branch, observation)
        third = choose_branch_action(branch, observation)

        self.assertEqual(first["direction"], "east")
        self.assertEqual(second, {"action": "idle"})
        self.assertEqual(third, {"action": "idle"})

    def test_begin_branch_step_builds_request_for_chosen_action(self):
        memory = Memory()
        snapshot = capture_brain_snapshot(memory)
        forced_action = {
            "action": "move",
            "direction": "east",
        }
        branch = create_branch(snapshot, forced_first_action=forced_action)
        pending, request = begin_branch_step(branch, observation={}, choice="rule")
        self.assertIs(pending.branch, branch)
        self.assertEqual(pending.choice, "rule")
        self.assertEqual(pending.action, forced_action)
        self.assertEqual(
            request,
            {
                "type": "counterfactual_request",
                "candidates": [
                    {
                        "choice": "rule",
                        "decision": forced_action,
                    },
                ],
            },
        )

    def test_complete_branch_step_matches_pending_choice(self):
        forced_action = {
            "action": "move",
            "direction": "east",
        }
        branch = create_branch(capture_brain_snapshot(Memory()), forced_first_action=forced_action)
        pending, _request = begin_branch_step(branch, observation={"position": [1, 1], "energy": 10}, choice="rule")
        expected_result = {
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

        completed = complete_branch_step(pending, {
            "type": "counterfactual_response",
            "results": [expected_result],
        })

        self.assertIs(completed.branch, branch)
        self.assertEqual(completed.choice, "rule")
        self.assertEqual(completed.action, forced_action)
        self.assertEqual(completed.result, expected_result)
        self.assertIs(pending.branch, branch)
        self.assertEqual(completed.observation_after, expected_result["observation_after"])

    def test_complete_branch_step_requires_next_observation(self):
        forced_action = {
            "action": "move",
            "direction": "east",
        }
        branch = create_branch(capture_brain_snapshot(Memory()), forced_first_action=forced_action)
        pending, _request = begin_branch_step(branch, observation={}, choice="rule")
        expected_result = {
            "choice": "rule",
            "succeeded": True,
            "result": "completed",
            "position_after": [2, 1],
            "energy_after": 9,
        }

        with self.assertRaises(ValueError):
            complete_branch_step(pending, {
                "type": "counterfactual_response",
                "results": [expected_result],
            })

    def test_next_branch_step_uses_previous_result_observation(self):
        forced_action = {
            "action": "move",
            "direction": "east",
        }

        branch = create_branch(capture_brain_snapshot(Memory()), forced_first_action=forced_action)
        initial_observation = {
            "world_id": "simulation-test",
            "position": [1, 1],
            "energy": 80,
            "nearby_objects": [],
            "north": "Wall",
            "east": "Wall",
            "south": "Wall",
            "west": "Wall",
        }

        pending, _ = begin_branch_step(branch, observation=initial_observation, choice="rule")
        expected_result = {
            "choice": "rule",
            "succeeded": True,
            "result": "completed",
            "position_after": [2, 1],
            "energy_after": 9,
            "observation_after": {
                "world_id": "simulation-test",
                "position": [2, 1],
                "energy": 9,
                "nearby_objects": [],
                "north": "Wall",
                "east": "Wall",
                "south": "Wall",
                "west": "Wall",
            }
        }

        completed = complete_branch_step(pending, {"type": "counterfactual_response", "results": [expected_result]})
        state = simulation_snapshot.BranchHorizonState(
            branch=branch,
            choice="rule",
            step_limit=2,
            completed_steps=[completed],
        )
        second_pending, second_request = begin_next_branch_step(state)

        self.assertIs(second_pending.branch, branch)
        self.assertIs(second_pending.observation_before, completed.observation_after)
        self.assertEqual(second_request["candidates"][0]["decision"], {"action": "idle"})

    def test_branch_horizon_stops_requesting_at_step_limit(self):
        forced_action = {"action": "move", "direction": "east"}
        branch = create_branch(
            capture_brain_snapshot(Memory()),
            forced_first_action=forced_action,
        )
        initial_observation = {
            "world_id": "simulation-test",
            "position": [1, 1],
            "energy": 80,
            "sensor_radius": 1,
            "visible_cells": [{"position": [1, 1], "type": "Empty"}],
            "nearby_objects": [],
            "north": "Wall",
            "east": "Wall",
            "south": "Wall",
            "west": "Wall",
        }
        first_observation_after = {
            **initial_observation,
            "position": [2, 1],
            "energy": 79,
            "visible_cells": [{"position": [2, 1], "type": "Empty"}],
            "last_action": {"type": "move", "succeeded": True},
        }
        second_observation_after = {
            **first_observation_after,
            "energy": 78,
            "last_action": {"type": "idle", "succeeded": True},
        }

        horizon, first_request = simulation_snapshot.begin_branch_horizon(
            branch, initial_observation, choice="rule", step_limit=2,
        )

        self.assertIsInstance(horizon, simulation_snapshot.BranchHorizonState)
        self.assertIs(horizon.branch, branch)
        self.assertEqual(horizon.choice, "rule")
        self.assertEqual(horizon.step_limit, 2)
        self.assertEqual(horizon.completed_steps, [])
        self.assertEqual(first_request, {
            "type": "counterfactual_request",
            "candidates": [{"choice": "rule", "decision": forced_action}],
        })

        first_result = {
            "choice": "rule",
            "succeeded": True,
            "result": "completed",
            "position_after": [2, 1],
            "energy_after": 79,
            "observation_after": first_observation_after,
        }
        second_request = simulation_snapshot.continue_branch_horizon(horizon, {
            "type": "counterfactual_response",
            "results": [first_result],
        })

        self.assertEqual(len(horizon.completed_steps), 1)
        self.assertIsNotNone(horizon.pending_step)
        self.assertEqual(horizon.pending_step.observation_before, first_observation_after)
        self.assertEqual(second_request, {
            "type": "counterfactual_request",
            "candidates": [{"choice": "rule", "decision": {"action": "idle"}}],
        })

        second_result = {
            "choice": "rule",
            "succeeded": True,
            "result": "completed",
            "position_after": [2, 1],
            "energy_after": 78,
            "observation_after": second_observation_after,
        }
        third_request = simulation_snapshot.continue_branch_horizon(horizon, {
            "type": "counterfactual_response",
            "results": [second_result],
        })

        self.assertIsNone(third_request)
        self.assertIsNone(horizon.pending_step)
        self.assertEqual(len(horizon.completed_steps), 2)
        self.assertEqual(horizon.completed_steps[0].result, first_result)
        self.assertEqual(horizon.completed_steps[1].result, second_result)


    def test_branch_horizon_updates_only_branch_memory_from_simulated_observation(self):
        real_memory = Memory()
        branch = create_branch(
            capture_brain_snapshot(real_memory),
            forced_first_action={"action": "move", "direction": "east"},
        )
        initial_observation = {
            "world_id": "simulation-test",
            "position": [1, 1],
            "energy": 80,
            "sensor_radius": 1,
            "visible_cells": [
                {"position": [1, 1], "type": "Empty"},
                {"position": [2, 1], "type": "Empty"},
            ],
            "nearby_objects": [],
            "north": "Wall",
            "east": "Empty",
            "south": "Wall",
            "west": "Wall",
        }
        state, _request = simulation_snapshot.begin_branch_horizon(
            branch, initial_observation, choice="rule", step_limit=1,
        )
        observation_after = {
            "world_id": "simulation-test",
            "position": [2, 1],
            "energy": 79,
            "sensor_radius": 1,
            "visible_cells": [
                {"position": [2, 1], "type": "Empty"},
                {"position": [3, 1], "type": "Battery"},
            ],
            "nearby_objects": [
                {"position": [3, 1], "type": "Battery"},
            ],
            "north": "Wall",
            "east": "Battery",
            "south": "Wall",
            "west": "Empty",
            "last_action": {
                "type": "move",
                "succeeded": True,
            },
        }

        simulation_snapshot.continue_branch_horizon(state, {
            "type": "counterfactual_response",
            "results": [{
                "choice": "rule",
                "succeeded": True,
                "result": "completed",
                "position_after": [2, 1],
                "energy_after": 79,
                "observation_after": observation_after,
            }],
        })

        self.assertEqual(branch.memory.step, 1)
        self.assertEqual(branch.memory.visit_count((2, 1)), 1)
        self.assertIn((3, 1), branch.memory.batteries())

        self.assertEqual(real_memory.step, 0)
        self.assertEqual(real_memory.visit_count((2, 1)), 0)
        self.assertNotIn((3, 1), real_memory.batteries())


    def test_branch_horizon_requires_positive_step_limit(self):
        branch = create_branch(
            capture_brain_snapshot(Memory()),
            forced_first_action={"action": "move", "direction": "east"},
        )
        observation = {
            "world_id": "simulation-test",
            "position": [1, 1],
            "energy": 80,
            "sensor_radius": 1,
            "visible_cells": [{"position": [1, 1], "type": "Empty"}],
            "nearby_objects": [],
            "north": "Wall",
            "east": "Empty",
            "south": "Wall",
            "west": "Wall",
        }

        with self.assertRaises(ValueError):
            simulation_snapshot.begin_branch_horizon(
                branch, observation, choice="rule", step_limit=0,
            )

    def test_completed_branch_horizon_rejects_another_response(self):
        branch = create_branch(
            capture_brain_snapshot(Memory()),
            forced_first_action={"action": "move", "direction": "east"},
        )
        initial_observation = {
            "world_id": "simulation-test",
            "position": [1, 1],
            "energy": 80,
            "sensor_radius": 1,
            "visible_cells": [{"position": [1, 1], "type": "Empty"}],
            "nearby_objects": [],
            "north": "Wall",
            "east": "Empty",
            "south": "Wall",
            "west": "Wall",
        }
        state, _request = simulation_snapshot.begin_branch_horizon(
            branch, initial_observation, choice="rule", step_limit=1,
        )
        observation_after = {
            **initial_observation,
            "position": [2, 1],
            "energy": 79,
            "visible_cells": [{"position": [2, 1], "type": "Empty"}],
            "east": "Wall",
            "west": "Empty",
            "last_action": {"type": "move", "succeeded": True},
        }
        response = {
            "type": "counterfactual_response",
            "results": [{
                "choice": "rule",
                "succeeded": True,
                "result": "completed",
                "position_after": [2, 1],
                "energy_after": 79,
                "observation_after": observation_after,
            }],
        }

        self.assertIsNone(simulation_snapshot.continue_branch_horizon(state, response))
        self.assertIsNone(state.pending_step)
        self.assertEqual(state.branch.memory.step, 1)
        self.assertEqual(len(state.completed_steps), 1)

        with self.assertRaises(ValueError):
            simulation_snapshot.continue_branch_horizon(state, response)

        self.assertEqual(state.branch.memory.step, 1)
        self.assertEqual(len(state.completed_steps), 1)
        self.assertEqual(state.branch.memory.visit_count((2, 1)), 1)


    def test_branch_horizon_records_each_immediate_reward(self):
        branch = create_branch(
            capture_brain_snapshot(Memory()),
            forced_first_action={"action": "move", "direction": "east"},
        )
        observation = {
            "world_id": "simulation-test",
            "position": [1, 1],
            "energy": 80,
            "sensor_radius": 1,
            "visible_cells": [{"position": [1, 1], "type": "Empty"}],
            "nearby_objects": [],
            "north": "Wall",
            "east": "Wall",
            "south": "Wall",
            "west": "Wall",
        }
        state, _request = simulation_snapshot.begin_branch_horizon(
            branch, observation, choice="rule", step_limit=3,
        )

        for action_type, energy_after in [("move", 79), ("idle", 78), ("idle", 76)]:
            observation_after = {
                **observation,
                "position": [2, 1],
                "energy": energy_after,
                "visible_cells": [{"position": [2, 1], "type": "Empty"}],
                "last_action": {"type": action_type, "succeeded": True},
            }
            request = simulation_snapshot.continue_branch_horizon(state, {
                "type": "counterfactual_response",
                "results": [{
                    "choice": "rule",
                    "succeeded": True,
                    "result": "completed",
                    "position_after": [2, 1],
                    "energy_after": energy_after,
                    "observation_after": observation_after,
                }],
            })

        self.assertIsNone(request)
        self.assertEqual(len(state.completed_steps), 3)
        # First visit: completion + new-cell bonus - one energy unit.
        self.assertEqual(state.completed_steps[0].reward, 0.19)
        # Revisits have no new-cell bonus; each uses its own energy delta.
        self.assertEqual(state.completed_steps[1].reward, 0.09)
        self.assertEqual(state.completed_steps[2].reward, 0.08)


    def test_branch_horizon_result_totals_completed_step_rewards(self):
        branch = create_branch(capture_brain_snapshot(Memory()))
        state = simulation_snapshot.BranchHorizonState(
            branch=branch,
            choice="rule",
            step_limit=3,
            completed_steps=[
                simulation_snapshot.CompletedBranchStep(
                    branch=branch,
                    choice="rule",
                    observation_before={},
                    action={"action": "idle"},
                    result={},
                    observation_after={},
                    reward=reward,
                )
                for reward in [0.14, 0.19, -0.25]
            ],
        )

        result = simulation_snapshot.branch_horizon_result(state)

        self.assertEqual(result.steps_completed, 3)
        self.assertAlmostEqual(result.cumulative_reward, 0.08)


    def test_branch_horizon_complete_stops_next_step_at_limit(self):
        branch = create_branch(capture_brain_snapshot(Memory()))
        steps = [
            simulation_snapshot.CompletedBranchStep(
                branch=branch,
                choice="rule",
                observation_before={},
                action={"action": "idle"},
                result={},
                observation_after={},
                reward=0.1,
            )
            for _ in range(3)
        ]
        state = simulation_snapshot.BranchHorizonState(
            branch=branch,
            choice="rule",
            step_limit=3,
            completed_steps=steps[:2],
        )

        self.assertFalse(simulation_snapshot.branch_horizon_complete(state))

        state.completed_steps.append(steps[2])

        self.assertTrue(simulation_snapshot.branch_horizon_complete(state))
        self.assertIsNone(begin_next_branch_step(state))


    def test_branch_horizon_stops_early_without_fabricating_rewards(self):
        branch = create_branch(capture_brain_snapshot(Memory()))
        state = simulation_snapshot.BranchHorizonState(
            branch=branch,
            choice="rule",
            step_limit=5,
            completed_steps=[
                simulation_snapshot.CompletedBranchStep(
                    branch=branch,
                    choice="rule",
                    observation_before={},
                    action={"action": "idle"},
                    result={},
                    observation_after={},
                    reward=reward,
                )
                for reward in [0.14, 0.19]
            ],
        )

        self.assertEqual(len(state.completed_steps), 2)
        self.assertFalse(simulation_snapshot.branch_horizon_complete(state))

        # No next action must use the same early-stop helper.
        with patch.object(simulation_snapshot, "choose_branch_action", return_value=None):
            self.assertIsNone(begin_next_branch_step(state))
        self.assertTrue(state.stopped_early)

        simulation_snapshot.stop_branch_horizon(state)

        self.assertTrue(simulation_snapshot.branch_horizon_complete(state))
        self.assertIsNone(begin_next_branch_step(state))
        result = simulation_snapshot.branch_horizon_result(state)
        self.assertEqual(result.steps_completed, 2)
        self.assertAlmostEqual(result.cumulative_reward, 0.33)
        self.assertEqual([step.reward for step in state.completed_steps], [0.14, 0.19])


if __name__ == "__main__":
    unittest.main()
