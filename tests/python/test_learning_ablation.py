import unittest

import torch

from brain.learning.ablation import without_feature
from brain.learning.features import FEATURE_NAMES


class LearningAblationTests(unittest.TestCase):
    def test_zeroes_named_feature_without_changing_original_tensor(self):
        inputs = torch.ones((3, len(FEATURE_NAMES)))

        without_energy = without_feature(inputs, "energy")

        self.assertTrue(torch.equal(inputs, torch.ones((3, len(FEATURE_NAMES)))))
        self.assertTrue(torch.equal(without_energy[:, 0], torch.zeros(3)))
        self.assertTrue(torch.equal(without_energy[:, 1:], inputs[:, 1:]))

    def test_can_zero_any_named_feature(self):
        inputs = torch.ones((2, len(FEATURE_NAMES)))

        without_trust = without_feature(inputs, "memory_trust")

        self.assertTrue(torch.equal(without_trust[:, 11], torch.zeros(2)))

    def test_rejects_unknown_feature(self):
        with self.assertRaises(ValueError):
            without_feature(torch.ones((2, len(FEATURE_NAMES))), "unknown")

    def test_action_group_zeroes_all_action_columns(self):
        inputs = torch.ones((2, len(FEATURE_NAMES)))
        without_action = without_feature(inputs, "action")

        self.assertTrue(torch.equal(without_action[:, 4:7], torch.zeros(2, 3)))
        self.assertTrue(torch.equal(without_action[:, :4], inputs[:, :4]))
        self.assertTrue(torch.equal(without_action[:, 7:], inputs[:, 7:]))


if __name__ == "__main__":
    unittest.main()
