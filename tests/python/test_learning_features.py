import unittest

from brain.experience import Experience
from brain.learning.features import (
    ValueInput,
    encode_experience,
    encode_value_input,
)


def make_experience(**overrides) -> Experience:
    values = {
        "kind": "action",
        "event": "move",
        "step": 1,
        "goal": "explore",
        "action": "move",
        "target": None,
        "position_before": (1, 1),
        "position_after": (2, 1),
        "energy_before": 100,
        "energy_after": 99,
        "succeeded": True,
        "result": "completed",
    }
    values.update(overrides)
    return Experience(**values)


class LearningFeatureTests(unittest.TestCase):
    def test_experience_and_value_input_encode_identically(self):
        experience = make_experience(
            energy_before=72,
            goal="recharge",
            action="move_to",
            target=(5, 2),
            position_before=(3, 2),
            path_length_before=10,
            memory_trust_before=0.67,
            next_step_was_visited=True,
            reachable_before=True,
        )

        experience_vector = encode_experience(
            experience
        )
        value_input = ValueInput(
            energy=experience.energy_before,
            goal=experience.goal,
            action=experience.action,
            target=experience.target,
            position=experience.position_before,
            path_length=experience.path_length_before,
            memory_trust=experience.memory_trust_before,
            next_step_was_visited=experience.next_step_was_visited,
            reachable=experience.reachable_before,
        )
        value_input_vector = encode_value_input(
            value_input
        )

        self.assertEqual(
            experience_vector,
            value_input_vector,
        )

    def test_encode_energy_normalizes_energy(self):
        experience = make_experience(
            energy_before=72,
            goal="recharge",
            action="move_to",
            target=[5, 2],
            position_before=(3, 2),
            path_length_before=10,
            memory_trust_before=0.67
        )

        self.assertEqual(
            encode_experience(experience),
            [0.72, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, -0.04, 0.0, 0.2, 0.67, 0.0, 0.0, 0.0]
        )

    def test_encodes_reachable_move_to(self):
        value_input = ValueInput(
            energy=50,
            goal="recharge",
            action="move_to",
            target=(5, 5),
            position=(1, 1),
            path_length=8,
            memory_trust=None,
            next_step_was_visited=False,
            reachable=True,
        )

        vector = encode_value_input(value_input)

        self.assertEqual(
            vector[-2:],
            [1.0, 1.0],
        )

    def test_encodes_unreachable_move_to(self):
        value_input = ValueInput(
            energy=50,
            goal="recharge",
            action="move_to",
            target=(5, 5),
            position=(1, 1),
            path_length=None,
            memory_trust=None,
            next_step_was_visited=None,
            reachable=False,
        )

        vector = encode_value_input(value_input)

        self.assertEqual(
            vector[-2:],
            [1.0, 0.0],
        )

    def test_encodes_not_applicable_reachability(self):
        value_input = ValueInput(
            energy=50,
            goal="explore",
            action="move",
            target=None,
            position=(1, 1),
            path_length=None,
            memory_trust=None,
            next_step_was_visited=False,
            reachable=None,
        )

        vector = encode_value_input(value_input)

        self.assertEqual(
            vector[-2:],
            [0.0, 0.0],
        )


if __name__ == "__main__":
    unittest.main()
