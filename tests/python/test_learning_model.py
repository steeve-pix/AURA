import unittest

import torch
from torch import nn

from brain.learning.dataset import to_tensors
from brain.learning.features import FEATURE_NAMES
from brain.learning.model import ValueModel


class ValueModelTests(unittest.TestCase):
    def test_value_model_returns_one_prediction(self):
        model = ValueModel()

        x = torch.zeros(len(FEATURE_NAMES))

        output = model(x)

        self.assertEqual(
            output.shape,
            torch.Size([1]),
        )

    def test_value_model_loss_is_one_number(self):
        model = ValueModel()

        x = [
            [0.0] * len(FEATURE_NAMES),
            [0.0] * len(FEATURE_NAMES),
            [0.0] * len(FEATURE_NAMES),
        ]
        y = [0.09, 0.14, 1.07]

        x_tensor, y_tensor = to_tensors(x, y)
        predictions = model(x_tensor)

        loss_function = nn.MSELoss()
        loss = loss_function(
            predictions,
            y_tensor,
        )

        self.assertEqual(
            predictions.shape,
            y_tensor.shape,
        )
        self.assertEqual(
            loss.shape,
            torch.Size([]),
        )
        self.assertGreaterEqual(
            loss.item(),
            0.0,
        )

    def test_one_learning_step_changes_a_parameter(self):
        model = ValueModel()

        x = [
            [1.0] * len(FEATURE_NAMES),
            [0.5] * len(FEATURE_NAMES),
            [0.25] * len(FEATURE_NAMES),
        ]
        y = [0.09, 0.14, 1.07]
        x_tensor, y_tensor = to_tensors(x, y)

        loss_function = nn.MSELoss()
        optimizer = torch.optim.Adam(
            model.parameters(),
            lr=0.001,
        )

        first_parameter = next(
            model.parameters()
        )
        before = (
            first_parameter
            .detach()
            .clone()
        )

        optimizer.zero_grad()
        predictions = model(x_tensor)
        loss = loss_function(
            predictions,
            y_tensor,
        )
        loss.backward()
        optimizer.step()

        after = (
            first_parameter
            .detach()
            .clone()
        )

        self.assertFalse(
            torch.equal(before, after)
        )


if __name__ == "__main__":
    unittest.main()
