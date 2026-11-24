"""Coupled shared-world rollouts; unchanged per-agent 205/32 neural interface."""
from contextlib import nullcontext
from statistics import mean
import aura_python
import torch

from .aura_env import AuraEnv, encode_observation
from .aura_policy import outputs_to_action
from .balance_metrics import BalanceMetrics


class ArenaEnv(AuraEnv):
    is_arena = True

    def __init__(self, agents=4, parallel=True, spacing=12., *, quality_version=None):
        self._configure_quality(quality_version)
        self.agents = agents
        self.spacing = float(spacing)
        self.simulation = aura_python.AuraArena(agents, parallel, self.spacing)
        self.info = None

    @property
    def termination_mode(self):
        return 'balance_v2'

    @property
    def environment_contract(self):
        return {**super().environment_contract, 'environment_version': 5,
                'arena_agents': self.agents, 'arena_spacing': self.spacing, 'arena_collisions': True,
                'arena_collision_version': 1, 'arena_finished_agents': 'removed_until_wave_reset',
                'evaluation_summary': 'mean_per_agent_rounded_steps'}

    def reset(self):
        raise ValueError('Shared arena requires collect_batch or evaluate_arena')

    def step(self, action):
        raise ValueError('Shared arena requires simultaneous actions for all active agents')

    def collect_batch(self, policy, episodes, *, gradients=True, stochastic=True, seed=None,
                      service=None, progress=None, finished=None, record_policy=False):
        from .train_nn import discounted_returns
        if type(episodes) is not int or episodes < 1:
            raise ValueError('Positive episode count required')
        data = {key: [] for key in ('states_by_episode', 'log_probs_by_episode', 'returns_by_episode',
                                    'episode_scores', 'episode_returns', 'balance_metrics')}
        if record_policy:
            data.update(samples_by_episode=[], means_by_episode=[])
        data['contact_pairs'] = 0
        rng = torch.random.fork_rng(devices=[]) if seed is not None else nullcontext()
        with rng, (torch.enable_grad() if gradients else torch.no_grad()):
            if seed is not None:
                torch.manual_seed(seed)
            for base in range(0, episodes, self.agents):
                count = min(self.agents, episodes - base)
                obs = list(self.simulation.reset(count))
                trackers = [BalanceMetrics() for _ in range(count)]
                states, logs, rewards = ([[] for _ in range(count)] for _ in range(3))
                saved_samples, saved_means = ([[] for _ in range(count)] for _ in range(2)) if record_policy else (None, None)
                last_actions = [{} for _ in range(count)]
                while True:
                    if service:
                        service()
                    active = [i for i in range(count) if not obs[i]['done']]
                    if not active:
                        break
                    encoded = torch.stack([encode_observation(obs[i]) for i in active])
                    means = policy(encoded)
                    distribution = torch.distributions.Normal(means, policy.exploration_std)
                    samples = distribution.sample() if stochastic else means.detach()
                    probabilities = distribution.log_prob(samples).sum(dim=-1)
                    actions = [{} for _ in range(self.agents)]
                    for j, i in enumerate(active):
                        states[i].append(encoded[j].detach().clone())
                        logs[i].append(probabilities[j])
                        if record_policy:
                            saved_samples[i].append(samples[j].detach().cpu().clone())
                            saved_means[i].append(means[j].detach().cpu().clone())
                        actions[i] = outputs_to_action(samples[j].clamp(*policy.sample_bounds).detach(),
                                                       policy.action_transforms)
                        last_actions[i] = actions[i]
                    next_obs = list(self.simulation.step_actions(actions))
                    data['contact_pairs'] += next_obs[active[0]]['arena_contacts']
                    for i in range(count):
                        if i not in active:
                            next_obs[i] = obs[i]  # retain the sealed terminal reward/metrics
                    for i in active:
                        item = next_obs[i]
                        qualities = self.quality_components(item)
                        reward = 0. if item['terminated'] else qualities['balance_quality']
                        item.update(self.environment_contract, reward_qualities=qualities,
                                    reward_total=reward, reward=reward, reward_available=True)
                        trackers[i].update(item, reward)
                        item['balance_metrics'] = trackers[i].result()
                        rewards[i].append(reward)
                        if progress:
                            progress(base + i, i, item, actions[i], reward, sum(rewards[i]))
                    obs = next_obs
                # Stable episode order, irrespective of which agent finishes first.
                for i in range(count):
                    if record_policy:
                        data['samples_by_episode'].append(saved_samples[i])
                        data['means_by_episode'].append(saved_means[i])
                    data['states_by_episode'].append(states[i])
                    data['log_probs_by_episode'].append(logs[i])
                    data['returns_by_episode'].append(discounted_returns(rewards[i]))
                    data['episode_scores'].append(obs[i]['steps'])
                    data['episode_returns'].append(sum(rewards[i]))
                    data['balance_metrics'].append(trackers[i].result())
                    if finished:
                        finished(base + i, obs[i], last_actions[i], rewards[i][-1], sum(rewards[i]))
                self.info = obs[0]
        return data

    def evaluate_arena(self, policy, *, stochastic=False, seed=10042, service=None):
        data = self.collect_batch(policy, self.agents, gradients=False, stochastic=stochastic,
                                  seed=seed, service=service or getattr(self, 'runtime_service', None))
        metrics = data['balance_metrics']
        numeric = ('steps', 'balanced_steps', 'balanced_seconds', 'unbalanced_steps', 'longest_balanced_steps',
                   'recoveries', 'fall_rate', 'balanced_fraction', 'average_balance_quality', 'total_reward')
        result = {key: mean(row[key] for row in metrics) for key in numeric}
        for key in ('steps', 'balanced_steps', 'unbalanced_steps', 'longest_balanced_steps', 'recoveries'):
            result[key] = round(result[key])
        reasons = {row['termination_reason'] for row in metrics}
        result.update(termination_reason=next(iter(reasons)) if len(reasons) == 1 else 'mixed',
                      stochastic=stochastic, exploration_seed=seed if stochastic else None,
                      agent_metrics=metrics, summary='mean per agent; counts rounded')
        return result


def make_environment(agents=1, *, spacing=12., quality_version=None):
    return (AuraEnv(quality_version=quality_version) if agents == 1 else
            ArenaEnv(agents, spacing=spacing, quality_version=quality_version))


def checkpoint_environment(data):
    """Read-only replay selection. Contract validation still rejects other drift.

    Training resume deliberately uses make_environment's active default instead.
    """
    if data.get("collection_mode") == "independent":
        from .independent_env import IndependentEnv
        env = IndependentEnv(1, quality_version=data.get("quality_version"))
        from .experiment_rewards import apply_reward
        apply_reward(env,data.get("experimental_reward"))
        return env
    return make_environment(data.get('arena_agents', 1),
                            spacing=data.get('arena_spacing', 12.),
                            quality_version=data.get('quality_version'))
