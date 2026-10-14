import copy
import math
import unittest

import torch

from python.aura_policy import AuraPolicy, AuraValue
from python.residual_critic_experiment import residual_dataset, evaluate_residual, train_residual_critic


def dataset(returns):
    return {'returns_by_episode':returns,
            'states_by_episode':[[torch.full((205,),float(10*i+t)) for t in range(len(ep))] for i,ep in enumerate(returns)]}


class ResidualCriticTests(unittest.TestCase):
    def test_alignment_raw_return_scaling_and_fallback(self):
        critic=AuraValue(205)
        data=dataset([[500.,200.,0.],[700.]])
        prepared=residual_dataset(critic,data,[5.,2.],4.)
        states,targets,base,residuals,lengths=prepared
        self.assertEqual(states.shape,(4,205))
        self.assertEqual(targets.shape,(4,1))
        self.assertEqual(states[:,0].tolist(),[0.,1.,2.,10.])
        self.assertEqual(targets[:,0].tolist(),[5.,2.,0.,7.])
        self.assertEqual(base[:,0].tolist(),[5.,2.,4.,5.])
        self.assertEqual(residuals[:,0].tolist(),[0.,0.,-4.,2.])
        self.assertEqual(lengths,[3,1])
        self.assertFalse(residuals.requires_grad)
        with torch.no_grad():
            for parameter in critic.parameters(): parameter.zero_()
        measured=evaluate_residual(critic,prepared,2)
        self.assertEqual(measured['combined_mse'],5.)
        self.assertEqual(measured['coverage']['covered_count'],3)
        self.assertEqual(measured['coverage']['uncovered_count'],1)
        self.assertAlmostEqual(measured['coverage']['covered_combined_mse'],4./3.)
        self.assertEqual(measured['coverage']['uncovered_combined_mse'],16.)
        self.assertEqual(measured['timestep_mse'],5.)
        self.assertEqual(measured['bins'][0]['count'],4)
        self.assertIsNone(measured['bins'][1]['combined_mse'])

    def test_training_changes_only_critic_and_never_uses_validation_for_updates(self):
        torch.manual_seed(43)
        critic=AuraValue(205)
        duplicate=copy.deepcopy(critic)
        actor=AuraPolicy(205).requires_grad_(False)
        actor_before=[p.clone() for p in actor.parameters()]
        critic_before=[p.detach().clone() for p in critic.parameters()]
        training=dataset([[400.,200.],[600.]])
        states_before=[[s.clone() for s in ep] for ep in training['states_by_episode']]
        validation=dataset([[500.,200.,0.]])
        alternate=dataset([[50000.,20000.,0.]])
        first,_=train_residual_critic(critic,training,validation,updates=2)
        second,_=train_residual_critic(duplicate,training,alternate,updates=2)
        self.assertEqual(first['timestep_scaled_predictions'],[5.,2.])
        self.assertEqual(first['constant_scaled_prediction'],4.)
        self.assertEqual(first['validation_fallback_count'],1)
        self.assertEqual([row['update'] for row in first['measurements']],[0,1,2])
        self.assertTrue(all(math.isfinite(row['validation_combined_mse']) for row in first['measurements']))
        self.assertTrue(any(not torch.equal(p,before) for p,before in zip(critic.parameters(),critic_before)))
        self.assertTrue(all(torch.equal(p,q) for p,q in zip(critic.parameters(),duplicate.parameters())))
        self.assertTrue(all(torch.equal(p,q) for p,q in zip(actor.parameters(),actor_before)))
        self.assertTrue(all(p.grad is None for p in actor.parameters()))
        self.assertNotEqual(first['measurements'][-1]['validation_combined_mse'],second['measurements'][-1]['validation_combined_mse'])
        for ep,before in zip(training['states_by_episode'],states_before):
            self.assertTrue(all(torch.equal(s,b) for s,b in zip(ep,before)))

    def test_invalid_data_is_rejected(self):
        critic=AuraValue(205)
        for data,table,constant in ((dataset([]),[1.],0.),(dataset([[]]),[1.],0.),
                                  (dataset([[1.]]),[],0.),(dataset([[1.]]),[float('nan')],0.),
                                  (dataset([[1.]]),[1.],float('inf'))):
            with self.subTest(data=data),self.assertRaises(ValueError):
                residual_dataset(critic,data,table,constant)
        data=dataset([[1.]])
        data['states_by_episode'][0].append(torch.zeros(205))
        with self.assertRaises(ValueError): residual_dataset(critic,data,[1.],0.)
        data=dataset([[1.]])
        data['states_by_episode'][0][0]=torch.zeros(204)
        with self.assertRaises(ValueError): residual_dataset(critic,data,[1.],0.)
        for updates,rate in ((0,.001),(1,float('nan')),(1,0.)):
            with self.assertRaises(ValueError):
                train_residual_critic(critic,dataset([[1.]]),dataset([[1.]]),updates=updates,rate=rate)


if __name__=='__main__': unittest.main()
