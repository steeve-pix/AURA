import copy
import unittest
import torch
from python.aura_policy import AuraPolicy, AuraValue
from python.ppo_experiment import PPOConfig, gaussian_kl, clipped_loss, update_ppo, critic_metrics, policy_change
from tests.support.arena import fixture


class PPOTests(unittest.TestCase):
    def setUp(self):
        torch.manual_seed(42)
        self.env = fixture()
        self.actor = AuraPolicy(205, exploration_std=3.)
        self.critic = AuraValue(205)
        self.optimizer = torch.optim.Adam(self.actor.parameters(), lr=.001)
        self.co = torch.optim.Adam(self.critic.parameters(), lr=.001)

    def collect(self):
        return self.env.collect_batch(self.actor, 4, gradients=False, seed=123, record_policy=True)

    def test_latent_samples_align_and_recording_does_not_change_rollout(self):
        data = self.collect()
        ordinary = self.env.collect_batch(self.actor, 4, gradients=False, seed=123)
        self.assertEqual(data['episode_returns'], ordinary['episode_returns'])
        outside = 0
        for states,samples,means,logs in zip(data['states_by_episode'], data['samples_by_episode'],
                data['means_by_episode'], data['log_probs_by_episode']):
            self.assertEqual(len(states), len(samples))
            for sample,mu,logp in zip(samples,means,logs):
                expected = torch.distributions.Normal(mu, 3.).log_prob(sample).sum()
                self.assertTrue(torch.equal(logp,expected))
                outside += int(bool((sample.abs()>1).any()))
        self.assertGreater(outside, 0)

    def test_clip_removes_incentive_in_both_advantage_directions(self):
        ratios = torch.tensor([1.5,.5],requires_grad=True)
        loss = clipped_loss(ratios.log(),torch.tensor([1.,-1.]))
        self.assertAlmostEqual(loss.item(), -.2, places=6)
        loss.backward()
        self.assertTrue(torch.equal(ratios.grad,torch.zeros(2)))

    def test_exact_kl_and_action_dimension_sum(self):
        old,new = torch.zeros(2,32),torch.ones(2,32)*.1
        self.assertTrue(torch.allclose(gaussian_kl(old,new,.25),torch.ones(2)*2.56))
        self.assertTrue(torch.equal(gaussian_kl(old,old,.25),torch.zeros(2)))

    def test_overshoot_restores_actor_and_adam_but_critic_can_learn(self):
        data = self.collect()
        before, critic_before = copy.deepcopy(self.actor.state_dict()),copy.deepcopy(self.critic.state_dict())
        result = update_ppo(self.actor,self.optimizer,self.critic,self.co,data,PPOConfig(target_kl=1e-14))
        self.assertTrue(result['rejected_overshoot'])
        self.assertEqual(result['actor_optimizer_steps'],0)
        self.assertLess(result['mean_kl'],1e-14)
        self.assertFalse(self.optimizer.state)
        self.assertTrue(all(torch.equal(v,self.actor.state_dict()[k]) for k,v in before.items()))
        self.assertTrue(any(not torch.equal(v,self.critic.state_dict()[k]) for k,v in critic_before.items()))

    def test_valid_update_is_bounded_and_deterministic(self):
        data = self.collect()
        initial_a, initial_c = copy.deepcopy(self.actor.state_dict()),copy.deepcopy(self.critic.state_dict())
        results=[]
        for _ in range(2):
            self.actor.load_state_dict(initial_a);self.critic.load_state_dict(initial_c)
            optimizer=torch.optim.Adam(self.actor.parameters(),lr=.001)
            co=torch.optim.Adam(self.critic.parameters(),lr=.001)
            results.append(update_ppo(self.actor,optimizer,self.critic,co,data))
        self.assertEqual(results[0],results[1])
        self.assertGreater(results[0]['actor_optimizer_steps'],0)
        self.assertLessEqual(results[0]['mean_kl'],.01)
        self.assertTrue(all(p.grad is not None for p in self.critic.parameters()))

    def test_overshoot_restores_existing_adam_momentum_and_step_count(self):
        update_ppo(self.actor,self.optimizer,self.critic,self.co,self.collect())
        before_actor=copy.deepcopy(self.actor.state_dict())
        before_adam=copy.deepcopy(self.optimizer.state_dict())
        self.assertTrue(before_adam['state'])
        result=update_ppo(self.actor,self.optimizer,self.critic,self.co,self.collect(),PPOConfig(target_kl=1e-14))
        self.assertEqual(result['actor_optimizer_steps'],0)
        self.assertTrue(all(torch.equal(v,self.actor.state_dict()[k]) for k,v in before_actor.items()))
        actual=self.optimizer.state_dict()
        self.assertEqual(actual['param_groups'],before_adam['param_groups'])
        for parameter,state in before_adam['state'].items():
            for key,value in state.items():
                self.assertTrue(torch.equal(value,actual['state'][parameter][key]))

    def test_stale_or_clipped_policy_data_is_rejected(self):
        data = self.collect()
        data['samples_by_episode'][0][0].clamp_(-1,1)
        with self.assertRaisesRegex(ValueError,'raw actions'): update_ppo(self.actor,self.optimizer,self.critic,self.co,data)
        data = self.collect()
        with torch.no_grad(): next(self.actor.parameters()).add_(.01)
        with self.assertRaisesRegex(ValueError,'freshly collected'): update_ppo(self.actor,self.optimizer,self.critic,self.co,data)

    def test_held_out_metrics_and_no_change_diagnostic(self):
        data = self.collect()
        metrics=critic_metrics(self.critic,data)
        self.assertGreater(metrics['rmse_raw'],0.)
        self.assertEqual(metrics['states'],26)
        change=policy_change(self.actor,self.actor,data['states_by_episode'])
        self.assertEqual(change['mean_kl'],0.)
        self.assertEqual(change['mean_action_mean_change'],0.)

    def test_optimizer_ownership_and_invalid_configuration(self):
        with self.assertRaisesRegex(ValueError,'ownership'):
            update_ppo(self.actor,self.co,self.critic,self.optimizer,self.collect())
        for args in (dict(target_kl=0),dict(clip_ratio=1),dict(actor_steps=0),dict(critic_steps=True)):
            with self.assertRaises(ValueError):PPOConfig(**args)

    def test_extra_critic_fits_do_not_change_current_batch_actor_update(self):
        data = self.collect()
        initial_actor = copy.deepcopy(self.actor.state_dict())
        initial_critic = copy.deepcopy(self.critic.state_dict())
        actors, critics = [], []
        for fits in (1,20):
            self.actor.load_state_dict(initial_actor)
            self.critic.load_state_dict(initial_critic)
            optimizer = torch.optim.Adam(self.actor.parameters(),lr=.001)
            co = torch.optim.Adam(self.critic.parameters(),lr=.001)
            update_ppo(self.actor,optimizer,self.critic,co,data,PPOConfig(critic_steps=fits))
            self.assertTrue(all(int(state['step'])==fits for state in co.state.values()))
            actors.append(copy.deepcopy(self.actor.state_dict()))
            critics.append(copy.deepcopy(self.critic.state_dict()))
        self.assertTrue(all(torch.equal(v,actors[1][k]) for k,v in actors[0].items()))
        self.assertTrue(any(not torch.equal(v,critics[1][k]) for k,v in critics[0].items()))


if __name__=='__main__':unittest.main()
