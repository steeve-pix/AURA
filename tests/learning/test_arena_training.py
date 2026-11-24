"""Shared collection and bounded scheduling; short fixtures do not prove recovery."""
import copy
import tempfile
import unittest
from unittest.mock import patch

import torch
from python.aura_env import AuraEnv
from python.aura_policy import AuraPolicy
from python.train_nn import TrainingConfig, train_batch, create_critic, collect_critic_rollouts
from python.model_checkpoint import load_checkpoint
from cli.backend.training_run import TrainingRun
from cli.backend.nn_session import TrainingSession


from tests.support.arena import ScriptedArena, fixture


class ArenaTrainingTests(unittest.TestCase):
    def test_critic_progress_streams_before_batch_returns_and_counts_completions(self):
        seen=[]
        class Collector:
            is_arena=True
            def collect_batch(self,policy,episodes,**options):
                notify=options['progress']
                notify(1,1,dict(done=False,steps=24),{},0.,0.)
                self.assert_live()
                notify(1,1,dict(done=True,steps=48),{},0.,0.)
                notify(0,0,dict(done=False,steps=24),{},0.,0.)
                notify(0,0,dict(done=True,steps=48),{},0.,0.)
                return {'episode_scores':[48,48]}
            def assert_live(self):
                if seen!=[(0,2,24)]:raise AssertionError('Progress was buffered until batch completion')
        result=collect_critic_rollouts(Collector(),None,2,42,lambda *event:seen.append(event))
        self.assertEqual(seen,[(0,2,24),(1,2,48),(1,2,24),(2,2,48)])
        self.assertEqual(result['episode_scores'],[48,48])

    def test_configuration_limits(self):
        self.assertEqual(TrainingConfig(collection_mode="historical", batch_size=8, batches=10, agents=4).batches, 10)
        for values in (dict(batches=0), dict(batches=1001), dict(agents=5),
                       dict(batch_size=7, agents=4), dict(batch_size=129)):
            with self.assertRaises(ValueError):
                TrainingConfig(collection_mode="historical", **values)

    def test_collects_sorted_complete_episodes_and_seals_terminal_metrics(self):
        env = fixture()
        actor = AuraPolicy(205)
        finished = []
        data = env.collect_batch(actor, 8, seed=333,
                                 finished=lambda ep, obs, *_: finished.append((ep, obs)))
        self.assertEqual(data['episode_scores'], [5, 6, 7, 8]*2)
        self.assertEqual([ep for ep, _ in finished], list(range(8)))
        for (ep, obs), states, values in zip(finished, data['states_by_episode'], data['returns_by_episode']):
            self.assertEqual(len(states), obs['steps'])
            self.assertEqual(len(values), len(states))
            self.assertEqual(obs['reward_total'], 0.)
            self.assertEqual(values[-1], 0.)
            self.assertEqual(obs['balance_metrics']['recoveries'], 1)
            self.assertTrue(all(torch.isfinite(s).all() and s.numel()==205 for s in states))

    def test_shared_rollout_actor_gradient_is_finite_without_optimizer_updates(self):
        env = fixture()
        actor = AuraPolicy(205)
        critic, critic_optimizer = create_critic(env, TrainingConfig(collection_mode="historical", ))
        optimizer = torch.optim.Adam(actor.parameters())
        before = copy.deepcopy(actor.state_dict())
        result = train_batch(env, actor, optimizer, 4, update_parameters=False,
                             critic=critic, critic_optimizer=critic_optimizer, update_critic=False)
        self.assertEqual(result['episode_scores'], [5, 6, 7, 8])
        self.assertTrue(all(torch.isfinite(p.grad).all() for p in actor.parameters()))
        self.assertTrue(all(p.grad is None for p in critic.parameters()))
        self.assertTrue(all(torch.equal(value, actor.state_dict()[key]) for key, value in before.items()))

    def test_two_automatic_batches_save_pairs_without_next_command(self):
        config = TrainingConfig(collection_mode="historical", batch_size=8, batches=2, agents=4)
        with tempfile.TemporaryDirectory() as directory, patch('python.arena_env.ArenaEnv', fixture):
            with TrainingRun(config, directory=directory) as training:
                session = TrainingSession(training=training)
                events = []
                session.emit = lambda *event: events.append(event)
                session.train()
                self.assertEqual(session.update, 2)
                self.assertEqual(training.episodes_completed, 16)
                self.assertEqual([e[1:3] for e in events if e[0]=='run_progress'], [(1, 2), (2, 2)])
                saved = torch.load(training.path, weights_only=True)
                self.assertEqual(saved['training_schedule'], dict(episodes_per_batch=8, consecutive_batches=2, agents=4))
                self.assertEqual(saved['environment_version'], 5)
                self.assertEqual(saved['critic']['environment_contract'], training.env.environment_contract)
                self.assertEqual(saved['updates_completed'], 2)
                details = [e for e in events if e[0] == 'agent_detail']
                results = [e for e in events if e[0] == 'agent_result']
                self.assertEqual({e[1] for e in details}, {1, 2, 3, 4})
                self.assertEqual(len(results), 16)
                self.assertEqual([e[2] for e in results[:8]], list(range(1, 9)))
                self.assertTrue(all(len(e) == 47 for e in details))
                self.assertEqual(saved['arena_spacing'], 12.)
                self.assertTrue(saved['critic']['optimizer']['state'])
                with self.assertRaises(ValueError):
                    load_checkpoint(training.path, AuraEnv())
                path = training.path
            with TrainingRun(config, 'resume', checkpoint=path, directory=directory) as resumed:
                self.assertEqual(resumed.config.agents, 4)
                self.assertEqual(resumed.updates_completed, 2)

    def test_saved_spacing_is_preserved_by_resume_and_restart(self):
        config = TrainingConfig(collection_mode="historical", batch_size=8, agents=4)
        old = fixture(spacing=6.)
        with tempfile.TemporaryDirectory() as directory, patch('python.arena_env.ArenaEnv', fixture):
            with TrainingRun(config, directory=directory) as training:
                training.env = old
                training.policy.environment_contract = old.environment_contract
                training.save(0)
                path = training.path
            original = path.read_bytes()
            for mode in ('resume', 'restart'):
                with TrainingRun(config, mode, checkpoint=path, directory=directory) as training:
                    self.assertEqual(training.env.spacing, 6.)
                    self.assertEqual(training.policy.environment_contract['arena_spacing'], 6.)
                self.assertEqual(path.read_bytes(), original)

    def test_cancellation_preserves_initial_checkpoint_without_partial_updates(self):
        config = TrainingConfig(collection_mode="historical", batch_size=8, batches=2, agents=4)
        with tempfile.TemporaryDirectory() as directory, patch('python.arena_env.ArenaEnv', fixture):
            with TrainingRun(config, directory=directory) as training:
                session = TrainingSession(training=training)
                calls = 0
                def stop():
                    nonlocal calls
                    calls += 1
                    if calls == 3:
                        raise KeyboardInterrupt
                session.service = stop
                with self.assertRaises(KeyboardInterrupt):
                    session.train()
                saved = torch.load(training.path, weights_only=True)
                self.assertEqual(saved['episodes_completed'], 0)
                self.assertEqual(saved['updates_completed'], 0)
                self.assertFalse(saved['critic']['optimizer']['state'])


if __name__ == '__main__':
    unittest.main()
