import unittest

from brain.memory import Memory
from brain.reward import calculate_reward


class ExperienceTests(unittest.TestCase):
    @staticmethod
    def finish_move(
            memory: Memory,
            *,
            action: dict,
            position_before: tuple[int, int],
            position_after: tuple[int, int],
            path_length_before: int | None = None,
            path_length_after: int | None = None,
    ):
        memory.begin_experience(
            goal="explore" if action["action"] == "move" else "recharge",
            action=action,
            observation={"position": list(position_before), "energy": 90},
        )
        return memory.finish_pending_experience({
            "position": list(position_after),
            "energy": 89,
            "last_action": {
                "type": action["action"],
                "target": action.get("target"),
                "succeeded": True,
                "result": "completed",
                "path_length_before": path_length_before,
                "path_length_after": path_length_after,
            },
        })

    def test_begin_creates_pending_experience(self):
        memory = Memory()
        memory.step = 10

        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": [5, 2]},
            observation={"position": [2, 2], "energy": 90},
        )

        self.assertIsNotNone(memory.pending_experience)
        self.assertEqual(memory.pending_experience["step"], 10)
        self.assertEqual(memory.pending_experience["goal"], "recharge")
        self.assertEqual(memory.pending_experience["target"], (5, 2))

    def test_begin_records_trust_for_remembered_battery_target(self):
        memory = Memory()
        memory.step = 10
        target = (5, 2)
        memory.remember_battery(target)

        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": list(target)},
            observation={"position": [2, 2], "energy": 90},
        )

        self.assertEqual(
            memory.pending_experience["memory_trust_before"],
            memory.battery_trust(target),
        )

    def test_completed_experience_keeps_battery_trust_from_before_action(self):
        memory = Memory()
        memory.step = 10
        target = (5, 2)
        memory.remember_battery(target)
        expected_trust = memory.battery_trust(target)

        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": list(target)},
            observation={"position": [2, 2], "energy": 90},
        )
        experience = memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move_to",
                "target": list(target),
                "succeeded": True,
                "result": "completed",
                "path_length_before": 8,
                "path_length_after": 7,
            },
        })

        self.assertEqual(
            experience.memory_trust_before,
            expected_trust,
        )

    def test_begin_has_no_memory_trust_for_non_battery_target(self):
        memory = Memory()
        target = (5, 2)
        memory.remember_unknown(target)

        memory.begin_experience(
            goal="investigate",
            action={"action": "move_to", "target": list(target)},
            observation={"position": [2, 2], "energy": 90},
        )

        self.assertIsNone(
            memory.pending_experience["memory_trust_before"]
        )

    def test_begin_has_no_memory_trust_without_target(self):
        memory = Memory()

        memory.begin_experience(
            goal="explore",
            action={"action": "move", "direction": "east"},
            observation={"position": [2, 2], "energy": 90},
        )

        self.assertIsNone(
            memory.pending_experience["memory_trust_before"]
        )

    def test_experience_finishes_from_next_observation(self):
        memory = Memory()
        memory.step = 10
        before = {
            "position": [2, 2],
            "energy": 90,
        }
        action = {
            "action": "move_to",
            "target": [5, 2],
        }

        memory.begin_experience(
            goal="recharge",
            action=action,
            observation=before,
        )

        after = {
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": True,
            },
        }
        completed = memory.finish_pending_experience(after)

        self.assertEqual(len(memory.experiences), 1)
        experience = memory.experiences[0]
        self.assertIs(completed, experience)
        self.assertEqual(experience.position_before, (2, 2))
        self.assertEqual(experience.position_after, (3, 2))
        self.assertEqual(experience.energy_before, 90)
        self.assertEqual(experience.energy_after, 89)
        self.assertEqual(experience.result, "completed")

    def test_body_success_is_copied_to_experience(self):
        memory = Memory()
        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": [5, 2]},
            observation={"position": [2, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [2, 2],
            "energy": 90,
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": False,
            },
        })

        self.assertFalse(memory.experiences[0].succeeded)
        self.assertEqual(memory.experiences[0].result, "failed")

    def test_unreachable_result_is_copied_from_body_feedback(self):
        memory = Memory()
        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": [5, 2]},
            observation={"position": [2, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [2, 2],
            "energy": 90,
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": False,
                "result": "unreachable",
            },
        })

        experience = memory.experiences[0]
        self.assertFalse(experience.succeeded)
        self.assertEqual(experience.result, "unreachable")

    def test_successful_body_feedback_records_completed_result(self):
        memory = Memory()
        memory.begin_experience(
            goal="explore",
            action={"action": "move", "direction": "east"},
            observation={"position": [2, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move",
                "target": None,
                "succeeded": True,
                "result": "completed",
            },
        })

        self.assertEqual(memory.experiences[0].result, "completed")

    def test_move_experience_keeps_pre_action_next_step_visit_state(self):
        memory = Memory()
        memory.record_visit([3, 2])

        memory.begin_experience(
            goal="explore",
            action={"action": "move", "direction": "east"},
            observation={"position": [2, 2], "energy": 90},
        )
        experience = memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move",
                "target": None,
                "succeeded": True,
                "result": "completed",
            },
        })

        self.assertTrue(experience.next_step_was_visited)

    def test_far_move_to_uses_body_next_step_from_before_action(self):
        memory = Memory()
        memory.record_visit([3, 2])
        target = [20, 9]

        pending = memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": target},
            observation={
                "position": [2, 2],
                "energy": 90,
                "nearby_objects": [],
            },
        )

        self.assertIsNone(pending["next_step_was_visited"])

        experience = memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move_to",
                "target": target,
                "succeeded": True,
                "result": "completed",
                "path_length_before": 30,
                "path_length_after": 29,
                "next_step_before": [3, 2],
                "next_step_after": [4, 2],
            },
        })

        self.assertTrue(experience.next_step_was_visited)

    def test_continuing_far_move_to_uses_previous_next_step_after(self):
        memory = Memory()
        memory.record_visit([4, 2])
        target = [20, 9]

        pending = memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": target},
            observation={
                "position": [3, 2],
                "energy": 89,
                "nearby_objects": [],
                "last_action": {
                    "type": "move_to",
                    "target": target,
                    "succeeded": True,
                    "result": "completed",
                    "path_length_after": 29,
                    "next_step_after": [4, 2],
                },
            },
        )

        self.assertTrue(pending["next_step_was_visited"])
        self.assertEqual(pending["path_length_before"], 29)

    def test_battery_is_an_outcome_not_a_result(self):
        memory = Memory()
        memory.begin_experience(
            goal="investigate",
            action={"action": "investigate", "target": [5, 2]},
            observation={"position": [4, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [4, 2],
            "energy": 90,
            "nearby_objects": [{"type": "Battery", "position": [5, 2]}],
            "last_action": {
                "type": "investigate",
                "target": [5, 2],
                "succeeded": True,
                "result": "completed",
            },
        })

        experience = memory.experiences[0]
        self.assertEqual(experience.result, "completed")
        self.assertEqual(experience.outcome, "Battery")

    def test_idle_action_does_not_create_pending_experience(self):
        memory = Memory()

        memory.begin_experience(
            goal="explore",
            action={"action": "idle"},
            observation={"position": [2, 2], "energy": 90},
        )

        self.assertIsNone(memory.pending_experience)
        self.assertEqual(memory.experiences, [])

    def test_completed_experience_clears_pending_state(self):
        memory = Memory()
        memory.begin_experience(
            goal="explore",
            action={"action": "move", "direction": "east"},
            observation={"position": [2, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move",
                "target": None,
                "succeeded": True,
            },
        })

        self.assertEqual(len(memory.experiences), 1)
        self.assertIsNone(memory.pending_experience)

    def test_investigation_outcome_is_detected_from_next_observation(self):
        memory = Memory()
        memory.begin_experience(
            goal="investigate",
            action={"action": "investigate", "target": [5, 2]},
            observation={"position": [4, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [4, 2],
            "energy": 90,
            "nearby_objects": [{
                "type": "Battery",
                "position": [5, 2],
                "reachable": True,
                "path_length": 1,
            }],
            "last_action": {
                "type": "investigate",
                "target": [5, 2],
                "succeeded": True,
            },
        })

        self.assertEqual(memory.experiences[0].outcome, "Battery")

    def test_recorded_experience_contains_calculated_reward(self):
        memory = Memory()
        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": [5, 2]},
            observation={"position": [2, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "nearby_objects": [],
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": True,
            },
        })

        recorded = memory.experiences[0]
        self.assertEqual(recorded.reward, calculate_reward(recorded))

    def test_unvisited_destination_cell_is_recorded_as_new_visit(self):
        experience = self.finish_move(
            Memory(),
            action={"action": "move", "direction": "east"},
            position_before=(2, 2),
            position_after=(3, 2),
        )

        self.assertTrue(experience.visited_new_cell)

    def test_previously_visited_destination_is_not_recorded_as_new_visit(self):
        memory = Memory()
        memory.record_visit([3, 2])

        experience = self.finish_move(
            memory,
            action={"action": "move", "direction": "east"},
            position_before=(2, 2),
            position_after=(3, 2),
        )

        self.assertFalse(experience.visited_new_cell)

    def test_known_but_unvisited_destination_is_recorded_as_new_visit(self):
        memory = Memory()
        memory.remember_cell([3, 2], "Empty")

        experience = self.finish_move(
            memory,
            action={"action": "move", "direction": "east"},
            position_before=(2, 2),
            position_after=(3, 2),
        )

        self.assertTrue(experience.visited_new_cell)

    def test_failed_body_action_increments_failure_debug_counter(self):
        memory = Memory()
        memory.begin_experience(
            goal="recharge",
            action={"action": "move_to", "target": [5, 2]},
            observation={"position": [2, 2], "energy": 90},
        )

        memory.finish_pending_experience({
            "position": [2, 2],
            "energy": 90,
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": False,
                "result": "unreachable",
            },
        })

        self.assertEqual(memory.failure_debug()["body_action_failures"], 1)

    def test_plan_event_records_direct_strategic_experience(self):
        memory = Memory()
        memory.step = 12

        experience = memory.record_plan_event(
            event="plan_failed",
            goal="investigate",
            target=(5, 2),
            observation={"position": [4, 2], "energy": 89},
            reward=-0.40,
        )

        self.assertEqual(experience.kind, "plan")
        self.assertEqual(experience.event, "plan_failed")
        self.assertEqual(experience.goal, "investigate")
        self.assertEqual(experience.target, (5, 2))
        self.assertEqual(experience.position_before, (4, 2))
        self.assertEqual(experience.position_after, (4, 2))
        self.assertEqual(experience.energy_before, 89)
        self.assertEqual(experience.energy_after, 89)
        self.assertFalse(experience.succeeded)
        self.assertEqual(experience.result, "plan_failed")
        self.assertEqual(experience.reward, -0.40)
        self.assertIs(memory.experiences[-1], experience)

    def test_plan_failure_does_not_increment_body_failure_counter(self):
        memory = Memory()

        memory.record_plan_event(
            event="plan_failed",
            goal="recharge",
            target=(5, 2),
            observation={"position": [2, 2], "energy": 20},
            reward=-0.40,
        )

        self.assertEqual(memory.failure_debug()["body_action_failures"], 0)

    def test_reduced_route_cost_records_navigation_progress(self):
        experience = self.finish_move(
            Memory(),
            action={"action": "move_to", "target": [5, 2]},
            position_before=(2, 2),
            position_after=(3, 2),
            path_length_before=12,
            path_length_after=11,
        )

        self.assertEqual(experience.path_length_before, 12)
        self.assertEqual(experience.navigation_progress, 1)

    def test_arrival_uses_zero_after_cost_to_record_progress(self):
        experience = self.finish_move(
            Memory(),
            action={"action": "move_to", "target": [5, 2]},
            position_before=(4, 2),
            position_after=(5, 2),
            path_length_before=1,
            path_length_after=0,
        )

        self.assertEqual(experience.navigation_progress, 1)

    def test_non_navigation_action_has_no_navigation_progress(self):
        experience = self.finish_move(
            Memory(),
            action={"action": "move", "direction": "east"},
            position_before=(2, 2),
            position_after=(3, 2),
        )

        self.assertIsNone(experience.navigation_progress)

    def test_missing_route_cost_has_no_navigation_progress(self):
        experience = self.finish_move(
            Memory(),
            action={"action": "move_to", "target": [5, 2]},
            position_before=(2, 2),
            position_after=(3, 2),
        )

        self.assertIsNone(experience.path_length_before)
        self.assertIsNone(experience.navigation_progress)

    def test_move_to_records_reachable_before(self):
        memory = Memory()

        memory.begin_experience(
            goal="recharge",
            action={
                "action": "move_to",
                "target": [5, 2],
            },
            observation={
                "position": [2, 2],
                "energy": 90,
            }
        )

        experience = memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": True,
                "result": "completed",
                "reachable_before": True,
                "path_length_before": 3,
                "path_length_after": 2,
            }
        })

        self.assertIs(experience.reachable_before, True)

    def test_move_to_records_unreachable_before(self):
        memory = Memory()

        memory.begin_experience(
            goal="recharge",
            action={
                "action": "move_to",
                "target": [5, 2],
            },
            observation={
                "position": [2, 2],
                "energy": 90,
            }
        )

        experience = memory.finish_pending_experience({
            "position": [2, 2],
            "energy": 90,
            "last_action": {
                "type": "move_to",
                "target": [5, 2],
                "succeeded": False,
                "result": "unreachable",
                "reachable_before": False,
            }
        })

        self.assertIs(experience.reachable_before, False)

    def test_move_has_no_reachability(self):
        memory = Memory()

        memory.begin_experience(
            goal="explore",
            action={
                "action": "move",
                "direction": "east",
            },
            observation={
                "position": [2, 2],
                "energy": 90,
            },
        )

        experience = memory.finish_pending_experience({
            "position": [3, 2],
            "energy": 89,
            "last_action": {
                "type": "move",
                "succeeded": True,
                "result": "completed",
            },
        })

        self.assertIsNone(experience.reachable_before)


if __name__ == "__main__":
    unittest.main()
