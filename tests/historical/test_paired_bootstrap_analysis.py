"""Pairing and shared evaluation seeds must survive every bootstrap draw."""
import copy
import unittest

from python.experiments.paired_bootstrap_analysis import bootstrap_analysis, interval, paired_vectors


def policy(values):
    return {'episode_seeds': list(range(119001, 119033)),
            'episode_rewards': list(values), 'episode_discounted_returns': list(values),
            'episode_survival_steps': list(values)}


class PairedBootstrapTests(unittest.TestCase):
    def test_percentile_interpolation_and_constant_pair_difference(self):
        self.assertEqual(interval([1., 3.], [[0, 0], [1, 1]])['confidence_interval_95'], [1.05, 2.95])
        result = interval([2.] * 32, [[0] * 32, [31] * 32])
        self.assertEqual(result, {'mean_difference': 2., 'confidence_interval_95': [2., 2.]})

    def test_shared_seed_clusters_do_not_invent_160_independent_observations(self):
        initial = policy([0.] * 32)
        updated = policy(list(range(32)))
        batches = {label: {'timestep': updated, 'residual': updated} for label in 'ABCDE'}
        report = bootstrap_analysis(initial, batches, resamples=200)
        per = report['per_batch']['A']['reward']['timestep_minus_initial']
        aggregate = report['aggregate']['reward']['timestep_minus_initial']['conditional_on_five_updates']
        self.assertEqual(per, aggregate)
        self.assertEqual(report['aggregate']['reward']['residual_minus_timestep']['conditional_on_five_updates'][
                             'confidence_interval_95'], [0., 0.])
        self.assertEqual(report, bootstrap_analysis(initial, batches, resamples=200))

    def test_crossed_bootstrap_includes_variation_between_update_batches(self):
        initial = policy([0.] * 32)
        batches = {label: {'timestep': policy([float(i - 2)] * 32), 'residual': policy([float(i - 2)] * 32)}
                   for i, label in enumerate('ABCDE')}
        report = bootstrap_analysis(initial, batches, resamples=500)
        aggregate = report['aggregate']['reward']['residual_minus_initial']
        self.assertEqual(aggregate['conditional_on_five_updates']['confidence_interval_95'], [0., 0.])
        lower, upper = aggregate['crossed_update_and_seed_bootstrap']['confidence_interval_95']
        self.assertLess(lower, 0)
        self.assertGreater(upper, 0)

    def test_rejects_misaligned_pairs_and_nonfinite_values(self):
        initial = policy([1.] * 32)
        updated = copy.deepcopy(initial)
        updated['episode_seeds'].reverse()
        with self.assertRaises(ValueError):
            paired_vectors(initial, {'timestep': initial, 'residual': updated})
        updated = policy([float('nan')] * 32)
        with self.assertRaises(ValueError):
            paired_vectors(initial, {'timestep': initial, 'residual': updated})


if __name__ == '__main__':
    unittest.main()
