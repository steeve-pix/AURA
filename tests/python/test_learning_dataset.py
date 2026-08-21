import unittest

import torch

from brain.experience import Experience
from brain.learning.dataset import build_dataset, to_tensors


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
        "reward": 0.09,
    }
    values.update(overrides)
    return Experience(**values)


class LearningDatasetTests(unittest.TestCase):
    def test_three_experiences_produce_expected_tensor_shapes(self):
        experiences = [
            make_experience(step=1),
            make_experience(step=2, reward=0.19),
            make_experience(step=3, reward=0.14),
        ]

        x, y = build_dataset(experiences)
        x_tensor, y_tensor = to_tensors(x, y)

        self.assertEqual(
            x_tensor.shape,
            torch.Size([3, 12]),
        )
        self.assertEqual(
            y_tensor.shape,
            torch.Size([3,1]),
        )

    def test_two_action_experiences_produce_two_feature_rows_and_rewards(self):
        experience_a = make_experience(
            reward=0.09,
        )
        experience_b = make_experience(
            action="move_to",
            event="move_to",
            goal="recharge",
            target=(5, 2),
            reward=0.14,
        )

        x, y = build_dataset([
            experience_a,
            experience_b,
        ])

        self.assertEqual(len(x), 2)
        self.assertEqual(len(y), 2)
        self.assertTrue(
            all(len(row) == 12 for row in x)
        )
        self.assertEqual(
            y,
            [
                experience_a.reward,
                experience_b.reward,
            ],
        )

    def test_plan_experiences_are_skipped(self):
        action_experience = make_experience()
        plan_experience = make_experience(
            kind="plan",
            event="plan_completed",
            action="",
            reward=0.75,
        )

        x, y = build_dataset([
            action_experience,
            plan_experience,
        ])

        self.assertEqual(len(x), 1)
        self.assertEqual(
            y,
            [action_experience.reward],
        )


if __name__ == "__main__":
    unittest.main()
