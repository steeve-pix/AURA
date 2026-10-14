import math
import unittest

from python.critic_error_analysis import temporal_metrics


class CriticErrorAnalysisTests(unittest.TestCase):
    def test_bias_and_variation_use_exact_timesteps_not_pooled_bin_returns(self):
        targets = [[float(t+1) for t in range(60)], [float(t+3) for t in range(61)]]
        predictions = [[value+2 for value in episode] for episode in targets]
        bins = temporal_metrics(predictions, targets, constant=0.)
        first = bins[0]
        self.assertEqual(first['count'], 120)
        self.assertEqual(first['bias'], 2.)
        self.assertEqual(first['mae'], 2.)
        self.assertEqual(first['mse'], 4.)
        self.assertEqual(first['mean_true_return'], 31.5)
        self.assertEqual(first['mean_predicted_return'], 33.5)
        self.assertEqual(first['within_timestep_population_std'], 1.)
        self.assertEqual(first['variation_observations'], 120)
        self.assertEqual(first['constant_mse'], sum(x*x for episode in targets for x in episode[:60])/120)
        self.assertEqual(bins[1]['count'], 1)
        self.assertEqual(bins[1]['peerless_observations'], 1)
        self.assertIsNone(bins[1]['within_timestep_population_std'])
        self.assertEqual(bins[2]['count'], 0)
        self.assertIsNone(bins[2]['mse'])

    def test_boundary_timesteps_are_counted_once_and_weighted_mse_matches(self):
        targets = [[1.]*361, [2.]*300]
        predictions = [[0.]*361, [0.]*300]
        bins = temporal_metrics(predictions, targets, constant=1.5)
        self.assertEqual([row['count'] for row in bins], [120]*5+[61])
        self.assertEqual(bins[-1]['range'], '300+')
        mse = sum(row['mse']*row['count'] for row in bins)/661
        self.assertAlmostEqual(mse, (361+4*300)/661)
        self.assertEqual(bins[-1]['peerless_observations'], 61)
        self.assertTrue(all(math.isfinite(row['bias']) for row in bins))

    def test_invalid_data_is_rejected(self):
        for predictions, targets, constant in (([], [], 0.), ([[1.]], [], 0.),
                ([[1., 2.]], [[1.]], 0.), ([[float('nan')]], [[1.]], 0.),
                ([[1.]], [[1.]], float('inf')), ([[]], [[]], 0.)):
            with self.subTest(predictions=predictions), self.assertRaises(ValueError):
                temporal_metrics(predictions, targets, constant)


if __name__ == '__main__':
    unittest.main()
