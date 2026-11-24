"""Active environment, paired learning, and objective compatibility contracts."""
import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import torch
from python.aura_env import AuraEnv, LegacyAuraEnv
from python.aura_policy import AuraPolicy, CONTROLLED_ACTION_KEYS
from python.balance_quality import balance_quality
from python.model_checkpoint import save_checkpoint, load_checkpoint, validate_checkpoint_contract
from python.train_nn import TrainingConfig, create_critic, train_batch, evaluate_balance
from cli.backend.training_run import TrainingRun


class ScriptedSimulation:
    """Valid observation fixtures; no claim of physically feasible recovery."""
    termination_mode = 'balance_v2'
    episode_limit = 6

    def __init__(self, truncate=False):
        self.base = AuraEnv().simulation.reset()
        self.truncate = truncate
        self.ticks = 0

    def reset(self):
        self.ticks = 0
        return self.sample()

    def sample(self):
        obs = copy.deepcopy(self.base)
        state = ('UNBALANCED', 'UNBALANCED', 'BALANCED', 'UNBALANCED', 'BALANCED', 'BALANCED',
                 'BALANCED' if self.truncate else 'FALLEN')[self.ticks]
        obs.update(steps=self.ticks, time=self.ticks / 120., balance_state=state,
                   terminated=state == 'FALLEN', truncated=self.ticks == 6 and self.truncate,
                   termination_reason='fallen' if state == 'FALLEN' else
                   'time_limit' if self.ticks == 6 else 'none')
        obs['done'] = obs['terminated'] or obs['truncated']
        if self.ticks > 1:
            obs.update(left_foot_grounded=True, right_foot_grounded=False,
                       support_polygon=[[0., 0.], [1., 0.], [0., 1.]], support_margin=.2,
                       center_of_mass_velocity=[0., 0., 0.])
        return obs

    def step_action(self, action):
        self.ticks += 1
        return self.sample()


def environment(truncate=False, *, quality_version=None):
    env = AuraEnv(quality_version=quality_version)
    env.simulation = ScriptedSimulation(truncate)
    return env


class BalanceSystemTests(unittest.TestCase):
    def test_primary_contract_and_historical_opt_in(self):
        env = AuraEnv()
        state, obs = env.reset()
        self.assertEqual(env.termination_mode, 'balance_v2')
        self.assertEqual(env.reward_version, 3)
        self.assertEqual(env.environment_contract['environment_version'], 5)
        self.assertEqual(len(state), 205)
        self.assertTrue(torch.isfinite(state).all())
        self.assertEqual(len(CONTROLLED_ACTION_KEYS), 32)
        self.assertEqual(obs['balance_state'], 'UNBALANCED')
        self.assertFalse(obs['done'])
        self.assertEqual(LegacyAuraEnv().reward_version, 1)

    def test_reward_recovery_metrics_and_terminal_guard(self):
        env = environment()
        env.reset()
        for step in range(1, 7):
            state, reward, terminated, truncated, obs = env.step({})
            self.assertTrue(torch.isfinite(state).all())
            self.assertEqual(reward, 0. if terminated else balance_quality(obs))
            self.assertFalse(truncated)
            self.assertEqual(terminated, step == 6)
            if step == 2:
                self.assertEqual(obs['balance_metrics']['recoveries'], 0)  # initial landing
        metrics = obs['balance_metrics']
        self.assertEqual(metrics['balanced_steps'], 3)
        self.assertEqual(metrics['unbalanced_steps'], 2)
        self.assertEqual(metrics['longest_balanced_steps'], 2)
        self.assertEqual(metrics['recoveries'], 1)
        self.assertEqual(metrics['fall_rate'], 1.)
        ticks = env.simulation.ticks
        with self.assertRaisesRegex(ValueError, 'Episode ended'):
            env.step({})
        self.assertEqual(env.simulation.ticks, ticks)
        env.reset()
        self.assertEqual(env.metrics.result()['recoveries'], 0)

    def test_time_limit_preserves_quality_without_failure_penalty(self):
        env = environment(truncate=True)
        env.reset()
        for _ in range(6):
            _, reward, terminated, truncated, obs = env.step({})
        self.assertFalse(terminated)
        self.assertTrue(truncated)
        self.assertEqual(reward, balance_quality(obs))
        self.assertGreater(reward, 0.)
        with self.assertRaises(ValueError):
            env.step({})

    def test_separate_optimizer_updates_and_finite_gradients(self):
        env = environment()
        actor = AuraPolicy(205)
        optimizer = torch.optim.Adam(actor.parameters(), lr=.001)
        critic, critic_optimizer = create_critic(env, TrainingConfig(collection_mode="historical", agents=1, ))
        before_actor = [p.detach().clone() for p in actor.parameters()]
        before_critic = [p.detach().clone() for p in critic.parameters()]
        result = train_batch(env, actor, optimizer, 2, update_parameters=False,
                             critic=critic, critic_optimizer=critic_optimizer)
        self.assertEqual(result['training_algorithm'], 'actor_critic_mc')
        self.assertTrue(all(torch.equal(p, old) for p, old in zip(actor.parameters(), before_actor)))
        self.assertTrue(any(not torch.equal(p, old) for p, old in zip(critic.parameters(), before_critic)))
        critic_optimizer.zero_grad(set_to_none=True)
        before_critic = [p.detach().clone() for p in critic.parameters()]
        train_batch(env, actor, optimizer, 2, critic=critic, critic_optimizer=critic_optimizer, update_critic=False)
        self.assertTrue(any(not torch.equal(p, old) for p, old in zip(actor.parameters(), before_actor)))
        self.assertTrue(all(torch.equal(p, old) for p, old in zip(critic.parameters(), before_critic)))
        self.assertTrue(all(p.grad is None for p in critic.parameters()))
        self.assertTrue(all(torch.isfinite(p.grad).all() for p in actor.parameters()))
        with self.assertRaisesRegex(ValueError, 'requires its paired'):
            train_batch(env, actor, optimizer, 2)
        with self.assertRaisesRegex(ValueError, 'separate'):
            train_batch(env, actor, optimizer, 2, critic=critic, critic_optimizer=optimizer)

    def test_checkpoint_resume_pair_and_historical_rejection(self):
        with tempfile.TemporaryDirectory() as directory, patch('python.train_nn.AuraEnv', environment), \
                patch('cli.backend.training_run.AuraEnv', environment):
            with TrainingRun(TrainingConfig(collection_mode="historical", agents=1, batch_size=2), directory=directory) as run:
                train_batch(run.env, run.policy, run.optimizer, 2,
                            critic=run.critic, critic_optimizer=run.critic_optimizer)
                path = run.save(2)
                actor = copy.deepcopy(run.policy.state_dict())
                critic = copy.deepcopy(run.critic.state_dict())
                data = torch.load(path, weights_only=True)
                expected = train_batch(run.env, run.policy, run.optimizer, 2,
                                       critic=run.critic, critic_optimizer=run.critic_optimizer)
                expected_actor = copy.deepcopy(run.policy.state_dict())
                expected_critic = copy.deepcopy(run.critic.state_dict())
            with TrainingRun(TrainingConfig(collection_mode="historical", agents=1, ), 'resume', checkpoint=path, directory=directory) as resumed:
                self.assertEqual(resumed.updates_completed, 1)
                self.assertTrue(all(torch.equal(v, resumed.policy.state_dict()[k]) for k, v in actor.items()))
                self.assertTrue(all(torch.equal(v, resumed.critic.state_dict()[k]) for k, v in critic.items()))
                self.assertTrue(resumed.critic_optimizer.state)
                actual = train_batch(resumed.env, resumed.policy, resumed.optimizer, 2,
                                     critic=resumed.critic, critic_optimizer=resumed.critic_optimizer)
                self.assertEqual(actual['loss'], expected['loss'])
                self.assertEqual(actual['critic_loss'], expected['critic_loss'])
                self.assertTrue(all(torch.equal(v, resumed.policy.state_dict()[k]) for k, v in expected_actor.items()))
                self.assertTrue(all(torch.equal(v, resumed.critic.state_dict()[k]) for k, v in expected_critic.items()))
            for key, value in (('reward_version', 1), ('quality_version', 999), ('critic', {})):
                with self.assertRaises(ValueError):
                    validate_checkpoint_contract({**data, key: value}, environment())
            legacy = Path(directory) / 'historical.pt'
            policy = AuraPolicy(205)
            save_checkpoint(policy, torch.optim.Adam(policy.parameters()), 0, legacy)
            original = legacy.read_bytes()
            for mode in ('resume', 'restart'):
                with self.assertRaisesRegex(ValueError, 'different reward/termination'):
                    TrainingRun(TrainingConfig(collection_mode="historical", agents=1, ), mode, checkpoint=legacy, directory=directory)
                self.assertEqual(legacy.read_bytes(), original)
            load_checkpoint(legacy, LegacyAuraEnv())

    def test_previous_ground_contract_cannot_resume_silently(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'older_ground.pt'
            env = AuraEnv()
            actor = AuraPolicy(205)
            optimizer = torch.optim.Adam(actor.parameters(), lr=.001)
            critic, critic_optimizer = create_critic(env, TrainingConfig(collection_mode="historical", agents=1, ))
            actor.environment_contract = env.environment_contract
            save_checkpoint(actor, optimizer, 0, path, critic=critic, critic_optimizer=critic_optimizer)
            checkpoint = torch.load(path, weights_only=True)
            checkpoint.update(environment_version=4, floor_friction_version=2,
                              floor_friction_model='coulomb_contact_impulses')
            checkpoint.pop('ground_material', None)
            checkpoint.pop('articulation_projection', None)
            torch.save(checkpoint, path)
            historical_bytes = path.read_bytes()
            with self.assertRaisesRegex(ValueError, 'contract differs.*floor_friction_version'):
                load_checkpoint(path, env)
            self.assertEqual(path.read_bytes(), historical_bytes)

    def test_deterministic_and_stochastic_evaluation_are_read_only(self):
        actor = AuraPolicy(205)
        before = copy.deepcopy(actor.state_dict())
        rng = torch.get_rng_state()
        for stochastic in (False, True):
            first = evaluate_balance(environment(), actor, stochastic=stochastic)
            self.assertEqual(first, evaluate_balance(environment(), actor, stochastic=stochastic))
            self.assertEqual(first['recoveries'], 1)
        self.assertTrue(torch.equal(rng, torch.get_rng_state()))
        self.assertTrue(all(torch.equal(v, actor.state_dict()[k]) for k, v in before.items()))

    def test_cli_worker_uses_paired_training_and_reports_both_evaluations(self):
        from cli.backend.nn_session import TrainingSession
        from cli.backend.evaluate_model import evaluate_checkpoint
        with tempfile.TemporaryDirectory() as directory, patch('python.train_nn.AuraEnv', environment), \
                patch('python.arena_env.AuraEnv', environment):
            with TrainingRun(TrainingConfig(collection_mode="historical", agents=1, batch_size=2), directory=directory) as run:
                session = TrainingSession(training=run)
                events = []
                session.emit = lambda *values: events.append(values)
                session.train()  # one short fixture batch, not a physical training run
                self.assertTrue(run.critic_optimizer.state)
                saved = torch.load(run.path, weights_only=True)
                self.assertEqual(saved['training_algorithm'], 'actor_critic_mc')
                self.assertEqual(saved['reward_version'], 3)
                self.assertTrue(any(x[0] == 'critic_update' for x in events))
                self.assertEqual({x[1] for x in events if x[0] == 'balance'},
                                 {'training', 'deterministic', 'stochastic', 'best'})
                result = evaluate_checkpoint(run.path)
                self.assertEqual(result['deterministic']['recoveries'], 1)
                self.assertEqual(result['stochastic']['termination_reason'], 'fallen')


if __name__ == '__main__':
    unittest.main()
