"""Real spawned physics, checkpoint contracts, and failure atomicity."""
from copy import deepcopy
import multiprocessing as mp
from pathlib import Path
import tempfile
import unittest
import torch
from python.independent_env import IndependentEnv,episode_seed
from python.aura_policy import AuraPolicy,AuraValue
from python.train_nn import TrainingConfig,create_critic,train_batch,collect_critic_rollouts
from python.model_templates import NetworkDesign
from cli.backend.training_run import TrainingRun


class IndependentWorkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(1)

    def test_critic_streams_real_worker_steps_and_completed_episodes(self):
        actor=AuraPolicy(205,hidden_sizes=(8,))
        events=[]
        result=collect_critic_rollouts(IndependentEnv(2,diagnostic_limit=24),actor,2,789,
                                      lambda *event:events.append(event))
        self.assertTrue(any(completed==0 and steps>0 for completed,total,steps in events))
        self.assertEqual(events[-1][0:2],(2,2))
        self.assertEqual([event[0] for event in events],sorted(event[0] for event in events))
        self.assertEqual(result['episode_scores'],[24,24])

    def test_worker_count_does_not_change_assigned_physical_episodes(self):
        torch.manual_seed(42);actor=AuraPolicy(205,hidden_sizes=(8,))
        one=IndependentEnv(1,diagnostic_limit=8).collect_batch(actor,2,seed=789)
        two=IndependentEnv(2,diagnostic_limit=8).collect_batch(actor,2,seed=789)
        self.assertEqual(one['episode_seeds'],two['episode_seeds'])
        self.assertEqual(len(set(two['worker_pids'])),2)
        self.assertEqual(two['contact_pairs'],0)
        for a,b in zip(one['states_by_episode'],two['states_by_episode']):self.assertTrue(torch.equal(torch.stack(a),torch.stack(b)))
        self.assertEqual(two['episode_scores'],[8,8])
        self.assertTrue(all(m['termination_reason']=='diagnostic_budget' for m in two['balance_metrics']))

    def test_actor_and_critic_only_change_on_explicit_updates(self):
        env=IndependentEnv(2,diagnostic_limit=8);actor=AuraPolicy(205,hidden_sizes=(8,))
        critic,co=create_critic(env,TrainingConfig(agents=2,critic_design=NetworkDesign((8,))))
        before=deepcopy(actor.state_dict());cb=deepcopy(critic.state_dict())
        rows=env.collect_batch(actor,2,seed=1)
        self.assertTrue(all(torch.equal(v,actor.state_dict()[k]) for k,v in before.items()))
        loss=-torch.stack([v for ep in rows['log_probs_by_episode'] for v in ep]).mean();loss.backward()
        self.assertTrue(all(p.grad is None for p in critic.parameters()))
        from python.train_nn import train_critic
        train_critic(critic,co,rows['states_by_episode'],rows['returns_by_episode'])
        self.assertTrue(all(torch.equal(v,actor.state_dict()[k]) for k,v in before.items()))
        self.assertTrue(any(not torch.equal(v,critic.state_dict()[k]) for k,v in cb.items()))

    def test_resume_changes_workers_preserves_architecture_weights_and_optimizer(self):
        with tempfile.TemporaryDirectory() as directory:
            config=TrainingConfig(agents=2,actor_design=NetworkDesign((8,12),'ReLU'),critic_design=NetworkDesign((9,),'Sigmoid'))
            with TrainingRun(config,name='workers',directory=directory) as run:
                actor=deepcopy(run.policy.state_dict());critic=deepcopy(run.critic.state_dict());path=run.save_current()
            with TrainingRun(TrainingConfig(agents=3),'resume',checkpoint=path,directory=directory) as run:
                self.assertEqual(run.env.agents,3)
                self.assertTrue(all(torch.equal(v,run.policy.state_dict()[k]) for k,v in actor.items()))
                self.assertTrue(all(torch.equal(v,run.critic.state_dict()[k]) for k,v in critic.items()))
                self.assertEqual(run.worker_changes[-1]['after'],3)
                run.save_current()
            self.assertEqual(torch.load(path,weights_only=True)['training_schedule']['agents'],3)
            with TrainingRun(TrainingConfig(agents=2),'restart',checkpoint=path,directory=directory) as run:
                self.assertEqual(run.policy.design.hidden_sizes,(8,12))
                self.assertEqual(run.critic.design.activation,'Sigmoid')
                self.assertEqual(run.updates_completed,0)

    def test_cancel_does_not_leave_worker_processes(self):
        before={p.pid for p in mp.active_children()}
        env=IndependentEnv(2,diagnostic_limit=8);actor=AuraPolicy(205,hidden_sizes=(8,))
        def cancel():raise KeyboardInterrupt
        with self.assertRaises(KeyboardInterrupt):env.collect_batch(actor,2,service=cancel)
        self.assertFalse({p.pid for p in mp.active_children()}-before)

    def test_failed_worker_cannot_update_actor(self):
        actor=AuraPolicy(205,hidden_sizes=(8,));before=deepcopy(actor.state_dict())
        with torch.no_grad():next(actor.parameters()).fill_(float('nan'))
        with self.assertRaises((ValueError,RuntimeError)):IndependentEnv(2,diagnostic_limit=8).collect_batch(actor,2)
        self.assertTrue(torch.isnan(next(actor.parameters())).all())

    def test_network_configuration_and_output_semantics(self):
        for activation in ('Tanh','ReLU','Sigmoid','Identity'):
            actor=AuraPolicy(205,(32,8),activation=activation)
            self.assertIsInstance(actor.network[-1],torch.nn.Tanh)
            self.assertEqual(actor(torch.zeros(205)).shape,(32,))
            value=AuraValue(205,(8,),activation=activation)
            self.assertIsInstance(value.network[-1],torch.nn.Linear)
            self.assertEqual(value(torch.zeros(205)).shape,(1,))
        with self.assertRaises(ValueError):NetworkDesign((0,))
        with self.assertRaises(ValueError):NetworkDesign((64,),'unknown')


if __name__=='__main__':unittest.main()
