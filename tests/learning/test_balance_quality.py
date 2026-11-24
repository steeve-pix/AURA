"""Quality semantics on measurement fixtures; these do not prove recovery feasibility."""
import copy
import csv
import math
from pathlib import Path
import unittest

from python.balance_quality import BalanceQualityConfig, balance_quality, balance_quality_components
from tests.support.observations import observation


class BalanceQualityTests(unittest.TestCase):
    def test_reference_and_product(self):
        q = balance_quality_components(observation())
        self.assertEqual(q['balance_quality'], 1.)
        self.assertEqual(q.pop('balance_quality'), math.prod(q.values()))

    def test_posture_average_and_symmetry(self):
        values = []
        for degrees in (0, 10, 30, 60, 90, 180):
            up = math.cos(math.radians(degrees))
            values.append(balance_quality(observation(pelvis_uprightness=up, torso_uprightness=up)))
        self.assertEqual(values, sorted(values, reverse=True))
        self.assertLess(values[3], .02)
        asymmetric = balance_quality(observation(torso_uprightness=0.))
        self.assertAlmostEqual(asymmetric, (1. + math.exp(-9.)) / 2.)
        self.assertEqual(asymmetric, balance_quality(observation(pelvis_uprightness=0.)))

    def test_height_crouch_and_no_height_bonus(self):
        for ratio in (.75, .8, 1., 1.5):
            self.assertEqual(balance_quality(observation(pelvis_height=4.17*ratio, torso_height=6*ratio)), 1.)
        self.assertAlmostEqual(balance_quality(observation(pelvis_height=4.17*.5, torso_height=3.)), .5)
        self.assertEqual(balance_quality(observation(pelvis_height=4.17*.25)), 0.)

    def test_controlled_motion_and_limbs(self):
        self.assertEqual(balance_quality(observation(center_of_mass_velocity=[.05, .1, 0.],
            pelvis_angular_velocity=[0., .2, 0.], arm_angles=[2., 1., 2.], head_uprightness=-1.)), 1.)

    def test_balanced_hold_penalizes_drift_smoothly(self):
        qualities = [balance_quality(observation(center_of_mass_velocity=[speed, 0., 0.]))
                     for speed in (0., .08, .2, .4, .6)]
        self.assertEqual(qualities[0], qualities[1])
        self.assertEqual(qualities, sorted(qualities, reverse=True))
        self.assertLess(qualities[2], .9)
        self.assertLess(qualities[3], .5)

    def test_balanced_hold_penalizes_planted_foot_slip_only(self):
        still = balance_quality(observation())
        slipping = balance_quality(observation(left_foot_velocity=[.2, 0., 0.]))
        self.assertLess(slipping, still)
        airborne_moving = balance_quality(observation(left_foot_grounded=False,
            left_foot_velocity=[2., 0., 2.]))
        self.assertEqual(airborne_moving, still)

    def test_historical_recovery_gate_is_preserved_for_replay(self):
        from python.balance_quality_v2 import balance_quality_components
        calm = balance_quality_components(observation(balance_state='UNBALANCED'))
        moving = balance_quality_components(observation(balance_state='UNBALANCED',
            center_of_mass_velocity=[.2, 0., 0.], left_foot_velocity=[.5, 0., 0.]))
        self.assertEqual(calm['stationary_hold'], moving['stationary_hold'])
        self.assertEqual(calm['foot_grip'], moving['foot_grip'])
        self.assertEqual(calm['balance_quality'], moving['balance_quality'])

    def test_labels_cannot_exempt_motion_costs(self):
        results = [balance_quality_components(observation(balance_state=label,
            center_of_mass_velocity=[.2, 0., 0.], left_foot_velocity=[.5, 0., 0.]))
            for label in ('BALANCED', 'UNBALANCED', 'FALLEN')]
        self.assertEqual(results[0], results[1])
        self.assertEqual(results[1], results[2])
        self.assertLess(results[1]['stationary_hold'], 1.)
        self.assertLess(results[1]['foot_grip'], 1.)

    def test_rotation_quality_matches_control_envelope(self):
        values = [balance_quality_components(observation(pelvis_angular_velocity=[speed, 0., 0.],
            torso_angular_velocity=[speed, 0., 0.]))['body_rotation']
            for speed in (.2, .25, .4, .65, 1.)]
        self.assertEqual(values[:2], [1., 1.])
        self.assertAlmostEqual(values[2], .8, delta=.005)
        self.assertAlmostEqual(values[3], .2, delta=.005)
        self.assertLess(values[4], .005)

    def test_uncontrolled_motion_not_cancelled_by_posture(self):
        for fields in (dict(center_of_mass_velocity=[0., -4., 0.]),
                       dict(center_of_mass_velocity=[4., 0., 0.]),
                       dict(pelvis_angular_velocity=[0., 0., 4.], torso_angular_velocity=[0., 0., 4.])):
            self.assertLess(balance_quality(observation(**fields)), .01)

    def test_support_and_transient_semantics(self):
        self.assertEqual(balance_quality(observation(right_foot_grounded=False)), 1.)
        self.assertEqual(balance_quality(observation(left_foot_grounded=False, right_foot_grounded=False)), 0.)
        self.assertEqual(balance_quality(observation(support_polygon=[], support_margin=100.)), .25)
        self.assertEqual(balance_quality(observation(support_margin=-.2)), .25)
        self.assertLess(balance_quality(observation(support_margin=0.)), 1.)
        # A single airborne observation lowers quality; it never terminates an episode.

    def test_labels_flags_history_and_purity(self):
        obs = observation()
        original = copy.deepcopy(obs)
        self.assertEqual(balance_quality(obs), balance_quality(obs))
        self.assertEqual(obs, original)
        self.assertEqual(balance_quality(obs), balance_quality(observation(balance_state='FALLEN',
            terminated=True, truncated=True, done=True, steps=1200)))

    def test_finite_extremes(self):
        for speed in (0., 1., 1e100, 1e200):
            for up in (-1., 0., 1.):
                q = balance_quality_components(observation(pelvis_uprightness=up,
                    center_of_mass_velocity=[speed, -speed, speed]))
                self.assertTrue(all(math.isfinite(x) and 0 <= x <= 1 for x in q.values()))

    def test_invalid_measurements(self):
        for fields in (dict(pelvis_height=float('nan')), dict(support_margin=float('inf')),
                       dict(center_of_mass_velocity=[0., 0.]),
                       dict(support_polygon=[[0., float('nan')]]),
                       dict(neutral_height_references=dict(pelvis=0., torso=6., center_of_mass=3.))):
            with self.assertRaises(ValueError):
                balance_quality(observation(**fields))

    def test_invalid_settings(self):
        for fields in (dict(posture_scale_degrees=0.), dict(gravity=float('inf')),
                       dict(height_zero_ratio=.8), dict(marginal_support_quality=1.1),
                       dict(stationary_com_scale=0.), dict(foot_slip_scale=-1.)):
            with self.assertRaises(ValueError):
                BalanceQualityConfig(**fields)

    def test_archived_physical_collapse(self):
        from python.diagnostics.balance_quality_audit import observation as archived_observation
        path = Path(__file__).resolve().parents[2] / 'docs/traces/step128-physical/neutral.csv'
        with path.open() as file:
            rows = {int(row['step']): archived_observation(row) for row in csv.DictReader(file)}
        self.assertGreater(balance_quality(rows[100]), .99)
        self.assertLess(balance_quality(rows[210]), .25)
        self.assertLess(balance_quality(rows[300]), .005)
        self.assertEqual(balance_quality(rows[516]), 0.)


if __name__ == '__main__':
    unittest.main()
