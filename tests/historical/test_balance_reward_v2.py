"""Measurement fixtures check reward behavior, not simulated recoverability."""
import math
import unittest

from python.balance_reward_v2 import BalanceRewardV2, RewardV2Config


from tests.support.observations import observation


def stationary_reward(obs):
    evaluator = BalanceRewardV2();
    evaluator.reset(obs)
    return evaluator.update({**obs, 'steps': obs['steps'] + 1})


class RewardTests(unittest.TestCase):
    def test_formula_and_sum(self):
        r = stationary_reward(observation());
        c = RewardV2Config()
        self.assertAlmostEqual(r['reward_total'], c.support_weight + c.recovery_weight * (c.discount - 1))
        self.assertEqual(r['reward_total'], sum(r['reward_components'].values()))
        self.assertEqual(set(r['reward_components']), set(c.weights))

    def test_directional_tilt_ranking(self):
        values = []
        for degrees in (0, 8, 45, -45):
            up = math.cos(math.radians(degrees))
            values.append(stationary_reward(observation(pelvis_uprightness=up, torso_uprightness=up))['reward_total'])
        self.assertGreater(values[0], values[1]);
        self.assertGreater(values[1], values[2])
        self.assertLess(values[2], 0);
        self.assertEqual(values[2], values[3])
        # Roll and pitch share uprightness, and intentionally share the formula.

    def test_crouching_plateau_and_no_height_farming(self):
        upright = stationary_reward(observation())
        for ratio in (.8, 1.5):
            r = stationary_reward(observation(pelvis_height=4.17 * ratio, torso_height=6 * ratio))
            self.assertEqual(upright['reward_total'], r['reward_total'])
        e = BalanceRewardV2();
        low = observation(pelvis_height=2., torso_height=3.);
        e.reset(low)
        r = e.update(observation(steps=1))
        self.assertLessEqual(r['reward_components']['recovery'], 0)

    def test_rotation_and_uncontrolled_fall(self):
        standing = stationary_reward(observation())['reward_total']
        for fields in (dict(pelvis_angular_velocity=[0, 0, 3], torso_angular_velocity=[0, 0, 3]),
                       dict(center_of_mass_velocity=[0, -4, 0])):
            self.assertLess(stationary_reward(observation(**fields))['reward_total'], standing)

    def test_deadbands_allow_controlled_motion(self):
        self.assertEqual(stationary_reward(observation())['reward_total'], stationary_reward(observation(
            center_of_mass_velocity=[.2, .1, 0], pelvis_angular_velocity=[0, .2, 0]))['reward_total'])

    def test_one_foot_and_independent_limbs(self):
        r = stationary_reward(observation())['reward_total']
        self.assertEqual(r, stationary_reward(observation(right_foot_grounded=False))['reward_total'])
        self.assertEqual(r, stationary_reward(observation(head_uprightness=-1, arm_angles=[2, 1, .5]))['reward_total'])

    def test_airborne_and_polygon_transients(self):
        unsupported = observation(left_foot_grounded=False, right_foot_grounded=False, support_polygon=[])
        self.assertEqual(stationary_reward(unsupported)['reward_total'], 0)
        for change in (dict(left_foot_grounded=False, right_foot_grounded=False, support_polygon=[]),
                       dict(support_polygon=[]), dict(support_margin=-.2)):
            e = BalanceRewardV2();
            e.reset(observation());
            r = e.update(observation(steps=1, **change))
            self.assertGreater(r['reward_total'], .8)
            self.assertLess(r['reward_qualities']['support'], 1)

    def test_physical_recovery_and_collapse(self):
        e = BalanceRewardV2();
        e.reset(observation(pelvis_uprightness=.5, torso_uprightness=.5))
        rewards = []
        for step, angle in enumerate((35, 25, 15, 0), 1):
            up = math.cos(math.radians(angle))
            r = e.update(observation(steps=step, pelvis_uprightness=up, torso_uprightness=up))
            rewards.append(r['reward_total']);
            self.assertGreater(r['reward_components']['recovery'], 0)
        self.assertEqual(rewards, sorted(rewards))
        r = e.update(observation(steps=5, pelvis_uprightness=0, torso_uprightness=0,
                                 pelvis_height=1, torso_height=1, center_of_mass_velocity=[0, -3, 0]))
        self.assertLess(r['reward_total'], 0);
        self.assertLess(r['reward_components']['recovery'], 0)

    def test_labels_cannot_farm_reward(self):
        a, b = BalanceRewardV2(), BalanceRewardV2();
        a.reset(observation());
        b.reset(observation())
        for step in range(1, 12):
            self.assertEqual(a.update(observation(steps=step)), b.update(observation(
                steps=step, balance_state='UNBALANCED' if step % 2 else 'BALANCED')))

    def test_potential_telescopes_through_terminal(self):
        e = BalanceRewardV2();
        e.reset(observation());
        initial = e.previous_potential;
        total = 0
        for step in range(1, 7):
            terminal = step == 6
            r = e.update(observation(steps=step, pelvis_uprightness=.5 if step % 2 else 1,
                                     balance_state='FALLEN' if terminal else 'UNBALANCED', terminated=terminal,
                                     done=terminal))
            total += e.config.discount ** (step - 1) * r['reward_terms']['recovery']
        self.assertAlmostEqual(total, -initial)
        self.assertEqual(r['reward_components']['terminal'], -e.config.terminal_weight)
        self.assertEqual(r['reward_components']['support'], 0)
        self.assertLess(r['reward_total'], 0)
        with self.assertRaises(ValueError): e.update(observation(steps=7))

    def test_truncation_is_not_fall(self):
        e = BalanceRewardV2();
        e.reset(observation());
        r = e.update(observation(steps=1, truncated=True, done=True))
        self.assertEqual(r['reward_components']['terminal'], 0)
        self.assertEqual(r['reward_qualities']['recovery_potential'], 0)

    def test_finite_and_invalid_inputs(self):
        for key in ('pelvis_height', 'support_margin', 'pelvis_uprightness'):
            with self.assertRaises(ValueError): stationary_reward(observation(**{key: float('nan')}))
        r = stationary_reward(observation(center_of_mass_velocity=[1e200, -1e200, 0]))
        self.assertTrue(math.isfinite(r['reward_total']))
        self.assertEqual(r['reward_qualities']['motion'], 0)
        with self.assertRaises(ValueError): RewardV2Config(discount=0)

    def test_tick_and_version_contract(self):
        e = BalanceRewardV2()
        with self.assertRaises(ValueError): e.update(observation(steps=1))
        with self.assertRaises(ValueError): e.reset(observation(termination_mode='legacy'))
        e.reset(observation())
        with self.assertRaises(ValueError): e.update(observation(steps=2))

    def test_real_wrapper_and_legacy_block(self):
        from python.aura_env import AuraEnv, BalanceV2Diagnostics, encode_observation
        self.assertEqual(AuraEnv('balance_v2').reward_version, 3)
        env = BalanceV2Diagnostics();
        initial = env.reset()
        self.assertEqual(len(encode_observation(initial)), 205)
        from python.aura_policy import CONTROLLED_ACTION_KEYS
        final = env.step(dict.fromkeys(CONTROLLED_ACTION_KEYS, 0.))
        self.assertIsInstance(final, dict)
        self.assertEqual(final['reward_total'], sum(final['reward_components'].values()))
        self.assertEqual(final['reward_version'], 2)

    def test_checkpoint_objective_metadata_without_training(self):
        import tempfile
        from pathlib import Path
        import torch
        from python.aura_policy import AuraPolicy
        from python.aura_env import LegacyAuraEnv as AuraEnv
        from python.model_checkpoint import save_checkpoint, validate_checkpoint_contract
        env = AuraEnv();
        policy = AuraPolicy(env.observation_size)
        optimizer = torch.optim.Adam(policy.parameters(), lr=.001)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'untrained.pt'
            save_checkpoint(policy, optimizer, 0, path)
            checkpoint = torch.load(path, weights_only=True)
        self.assertEqual(checkpoint['reward_version'], 1)
        self.assertEqual(checkpoint['termination_mode'], 'legacy')
        self.assertEqual(checkpoint['updates_completed'], 0)
        validate_checkpoint_contract(checkpoint, env)
        for key in ('termination_mode', 'termination_version', 'reward_version', 'reward_name'):
            checkpoint.pop(key)
        validate_checkpoint_contract(checkpoint, env)
        for key, value in (('reward_version', 2), ('termination_mode', 'balance_v2'), ('termination_version', 2)):
            with self.subTest(key=key):
                changed = {**checkpoint, key: value}
                with self.assertRaisesRegex(ValueError, 'objective'): validate_checkpoint_contract(changed, env)


if __name__ == '__main__': unittest.main()
