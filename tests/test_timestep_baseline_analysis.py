import copy
import unittest

from python.timestep_baseline_analysis import compare_baselines


class TimestepBaselineTests(unittest.TestCase):
    def test_training_only_lookup_and_constant_fallback(self):
        training=[[4.,2.],[6.]]
        validation=[[5.,2.,0.],[7.]]
        predictions=[[4.,3.,1.],[6.]]
        before=copy.deepcopy((training,validation,predictions))
        report=compare_baselines(training,validation,predictions)
        self.assertEqual(report['timestep_scaled_predictions'],[5.,2.])
        self.assertEqual(report['training_observations_per_timestep'],[2,1])
        self.assertEqual(report['constant_scaled_prediction'],4.)
        self.assertEqual(report['uncovered_timesteps'],[2])
        overall=report['overall']
        self.assertEqual(overall['count'],4)
        self.assertEqual(overall['covered_count'],3)
        self.assertEqual(overall['uncovered_count'],1)
        self.assertEqual(overall['timestep_mse_with_fallback'],5.)
        self.assertAlmostEqual(overall['timestep_mse_covered_only'],4./3.)
        self.assertEqual(overall['constant_mse'],7.5)
        self.assertEqual(overall['critic_mse'],1.)
        self.assertEqual((training,validation,predictions),before)
        validation[0][0]=1000.
        changed=compare_baselines(training,validation,predictions)
        self.assertEqual(changed['timestep_scaled_predictions'],report['timestep_scaled_predictions'])
        self.assertEqual(changed['constant_scaled_prediction'],4.)

    def test_bins_count_boundaries_once_and_weight_full_mse(self):
        report=compare_baselines([[1.]*300],[[1.]*361,[2.]*60],[[0.]*361,[0.]*60])
        self.assertEqual([row['count'] for row in report['bins']],[120,60,60,60,60,61])
        self.assertEqual(report['overall']['uncovered_count'],61)
        self.assertEqual(report['bins'][-1]['uncovered_count'],61)
        self.assertIsNone(report['bins'][-1]['timestep_mse_covered_only'])
        for metric in ('timestep_mse_with_fallback','constant_mse','critic_mse'):
            weighted=sum(row[metric]*row['count'] for row in report['bins'])/421
            self.assertAlmostEqual(weighted,report['overall'][metric])

    def test_empty_bins_are_explicit(self):
        report=compare_baselines([[1.]],[[2.]],[[1.]])
        self.assertEqual(report['overall']['uncovered_count'],0)
        self.assertEqual(report['bins'][1]['count'],0)
        self.assertIsNone(report['bins'][1]['timestep_mse_with_fallback'])

    def test_invalid_or_misaligned_inputs_are_rejected(self):
        for train,val,pred in (([],[[1.]],[[1.]]),([[]],[[1.]],[[1.]]),
                ([[1.]],[],[]),([[1.]],[[1.]],[[1.,2.]]),([[1.]],[[1.]],[[1.],[2.]]),
                ([[float('nan')]],[[1.]],[[1.]]),([[1.]],[[float('inf')]],[[1.]]),
                ([[1.]],[[1.]],[[float('nan')]])):
            with self.subTest(train=train,val=val),self.assertRaises(ValueError):
                compare_baselines(train,val,pred)


if __name__=='__main__': unittest.main()
