"""Check diagnostic accounting without changing any reward or simulation behavior."""
import math
import unittest
from python.balance_quality_v2 import balance_quality_components
from python.diagnostics.reward_component_audit import record, summarize
from tests.support.observations import observation


def measured(**fields):
    obs=observation(center_of_mass_height=3.75625,steps=1,**fields)
    obs['reward_qualities']=balance_quality_components(obs)
    reward=0. if obs['terminated'] else obs['reward_qualities']['balance_quality']
    return record(obs,reward)


class ComponentAuditTests(unittest.TestCase):
    def test_support_floor_is_not_final_reward_floor(self):
        row=measured(balance_state='UNBALANCED',support_margin=-.15,
                     pelvis_uprightness=0.,torso_uprightness=0.)
        self.assertEqual(row['support'],.25)
        self.assertLess(row['balance_quality'],.001)
        summary=summarize([row])
        self.assertEqual(summary['support_floor_steps'],1)
        self.assertEqual(summary['floor_with_valid_polygon'],1)
        self.assertAlmostEqual(summary['factor_removal_quality_delta']['support'],3*row['balance_quality'])

    def test_terminal_reward_and_raw_quality_are_distinct(self):
        row=measured(terminated=True,balance_state='FALLEN')
        self.assertEqual(row['reward'],0.)
        self.assertEqual(row['balance_quality'],1.) # A fixture can expose terminal masking.
        self.assertEqual(summarize([row])['total_reward'],0.)

    def test_unbalanced_gate_counterfactual_does_not_mutate_state(self):
        row=measured(balance_state='UNBALANCED',center_of_mass_velocity=[.4,0,0],
                     left_foot_velocity=[.2,0,0],right_foot_velocity=[.2,0,0])
        self.assertEqual(row['stationary_hold'],1.)
        self.assertEqual(row['foot_grip'],1.)
        self.assertLess(row['always_hold_quality'],row['balance_quality'])
        self.assertTrue(row['entry_failures']['horizontal_speed'])
        self.assertGreater(summarize([row])['mean_quality_gate_exemption'],0.)
        self.assertEqual(row['state'],'UNBALANCED')

    def test_airborne_is_separate_from_marginal_support(self):
        row=measured(left_foot_grounded=False,right_foot_grounded=False)
        result=summarize([row])
        self.assertEqual(row['reward'],0.)
        self.assertEqual(result['grounded_steps'],0)
        self.assertIsNone(result['support_floor_fraction_of_grounded'])
        self.assertEqual(summarize([]),dict(steps=0))

    def test_arithmetic_mean_of_products_is_not_product_of_means(self):
        a=measured(left_foot_grounded=False,right_foot_grounded=False)
        b=measured(pelvis_uprightness=0.,torso_uprightness=0.)
        s=summarize([a,b])
        self.assertGreater(math.prod(s['mean_components'].values()),s['mean_quality'])


if __name__=='__main__':unittest.main()
