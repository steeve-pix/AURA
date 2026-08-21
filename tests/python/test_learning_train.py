import unittest

import torch

from brain.learning.dataset import to_tensors
from brain.learning.model import ValueModel
from brain.learning.train import baseline_mse, train_model


class LearningTrainTests(unittest.TestCase):
    def test_weighted_training_rejects_misaligned_weights(self):
        model = ValueModel()
        x_train = torch.zeros((3, 12))
        y_train = torch.zeros((3, 1))
        weights = torch.ones((2, 1))

        with self.assertRaises(ValueError):
            train_model(
                model,
                x_train,
                y_train,
                epochs=1,
                sample_weights=weights,
            )

    def test_baseline_predicts_training_mean(self):
        y_train = torch.tensor([
            [0.1],
            [0.3],
        ])
        y_test = torch.tensor([
            [0.2],
            [0.4],
        ])

        loss = baseline_mse(
            y_train,
            y_test,
        )

        self.assertAlmostEqual(
            loss,
            0.02,
            places=6,
        )

    def test_training_reduces_loss(self):
        torch.manual_seed(42)

        model = ValueModel()
        x = [
            [0.0] * 12,
            [0.25] * 12,
            [0.5] * 12,
            [0.75] * 12,
            [1.0] * 12,
        ]
        y = [0.09, 0.14, 0.19, 0.24, 0.29]
        x_train, y_train = to_tensors(x, y)

        losses = train_model(
            model,
            x_train,
            y_train,
            epochs=100,
        )

        self.assertEqual(len(losses), 100)
        self.assertLess(
            losses[-1],
            losses[0],
        )


if __name__ == "__main__":
    unittest.main()
