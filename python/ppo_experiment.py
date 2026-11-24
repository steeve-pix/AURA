"""Experimental PPO-Clip with Monte Carlo advantages; production stays unchanged.

Uses latent Gaussian actions, fixed exploration std, detached pre-fit value
baselines and finite-horizon returns. Exact old-to-new Gaussian KL is evaluated
on the collection states. An overshooting Adam step restores both weights and
optimizer state, then ends the update. This bound is empirical on those states,
not a global guarantee. No GAE, entropy schedule or new dependency is introduced.
"""
from copy import deepcopy
from dataclasses import dataclass
from math import isfinite
import torch
from .train_nn import prepare_critic_data, train_critic


@dataclass(frozen=True)
class PPOConfig:
    clip_ratio: float = .2
    target_kl: float = .01
    actor_steps: int = 8
    critic_steps: int = 1

    def __post_init__(self):
        if not isfinite(self.clip_ratio) or not 0 < self.clip_ratio < 1:
            raise ValueError('clip_ratio must be in (0,1)')
        if not isfinite(self.target_kl) or self.target_kl <= 0:
            raise ValueError('target_kl must be positive and finite')
        if any(type(n) is not int or n < 1 for n in (self.actor_steps, self.critic_steps)):
            raise ValueError('Positive integer optimizer step counts required')


def gaussian_kl(old_means, new_means, std):
    """Per-state sum across latent action dimensions: KL(old || new)."""
    if not isfinite(std) or std <= 0 or old_means.shape != new_means.shape or old_means.ndim != 2:
        raise ValueError('Matching [states, actions] means and positive std required')
    if not torch.isfinite(old_means).all() or not torch.isfinite(new_means).all():
        raise ValueError('Finite policy means required')
    return ((old_means - new_means) / std).square().sum(-1) * .5


def clipped_loss(log_ratio, advantages, clip_ratio=.2):
    if log_ratio.shape != advantages.shape or log_ratio.ndim != 1:
        raise ValueError('Matching scalar log-ratios and advantages required')
    if not torch.isfinite(log_ratio).all() or not torch.isfinite(advantages).all():
        raise ValueError('Finite ratios and advantages required')
    ratio = log_ratio.exp()
    return -torch.minimum(ratio * advantages,
                          ratio.clamp(1-clip_ratio, 1+clip_ratio) * advantages).mean()


def critic_metrics(critic, data):
    states, targets = prepare_critic_data(critic, data['states_by_episode'], data['returns_by_episode'])
    with torch.no_grad():
        prediction = critic(states)
        if prediction.shape != targets.shape or not torch.isfinite(prediction).all():
            raise ValueError('Finite scalar critic predictions required')
        error = targets - prediction
        variance = targets.var(unbiased=False).item()
        return dict(states=len(states), mse_scaled=error.square().mean().item(),
                    rmse_raw=100 * error.square().mean().sqrt().item(),
                    explained_variance=1-error.var(unbiased=False).item()/variance if variance > 1e-12 else None,
                    target_variance_scaled=variance,
                    zero_predictor_mse_scaled=targets.square().mean().item())


def policy_change(actor_before, actor_after, states):
    states = torch.stack([state for episode in states for state in episode])
    with torch.no_grad():
        before, after = actor_before(states), actor_after(states)
        kl = gaussian_kl(before, after, actor_after.exploration_std)
        delta = (after - before).abs()
        return dict(mean_kl=kl.mean().item(), max_kl=kl.max().item(),
                    p95_kl=torch.quantile(kl, .95).item(),
                    mean_action_mean_change=delta.mean().item(), max_action_mean_change=delta.max().item(),
                    saturated_mean_fraction=(after.abs() > .95).float().mean().item())


def update_ppo(actor, optimizer, critic, critic_optimizer, data, config=PPOConfig()):
    actor_ids = {id(p) for p in actor.parameters()}
    critic_ids = {id(p) for p in critic.parameters()}
    if actor_ids & critic_ids:
        raise ValueError('Actor and critic must be separate')
    for opt, expected in ((optimizer, actor_ids), (critic_optimizer, critic_ids)):
        if {id(p) for group in opt.param_groups for p in group['params']} != expected:
            raise ValueError('Optimizer ownership mismatch')
    states, targets = prepare_critic_data(critic, data['states_by_episode'], data['returns_by_episode'])
    device = next(actor.parameters()).device
    states = states.to(device)
    samples = torch.stack([v for ep in data['samples_by_episode'] for v in ep]).to(device).detach()
    old_means = torch.stack([v for ep in data['means_by_episode'] for v in ep]).to(device).detach()
    old_logs = torch.stack([v for ep in data['log_probs_by_episode'] for v in ep]).to(device).detach()
    if samples.shape != old_means.shape or len(samples) != len(states) or old_logs.shape != (len(states),):
        raise ValueError('Stored policy data must align with states')
    with torch.no_grad():
        old_distribution = torch.distributions.Normal(old_means, actor.exploration_std)
        if not torch.allclose(old_distribution.log_prob(samples).sum(-1), old_logs, atol=1e-5, rtol=1e-5):
            raise ValueError('Stored raw actions and old log probabilities disagree')
        if not torch.allclose(actor(states), old_means, atol=1e-6, rtol=1e-6):
            raise ValueError('PPO requires freshly collected on-policy data')
        advantage = (targets - critic(states.to(next(critic.parameters()).device))).flatten().to(device)
        advantage = (advantage - advantage.mean()) / advantage.std(unbiased=False).clamp_min(1e-8)
    if not torch.isfinite(advantage).all():
        raise ValueError('Finite detached advantages required')
    critic_optimizer.zero_grad(set_to_none=True)
    accepted, rejected, max_attempted_kl, loss_value = 0, False, 0., None
    for _ in range(config.actor_steps):
        parameters, adam = deepcopy(actor.state_dict()), deepcopy(optimizer.state_dict())
        optimizer.zero_grad(set_to_none=True)
        means = actor(states)
        logs = torch.distributions.Normal(means, actor.exploration_std).log_prob(samples).sum(-1)
        loss = clipped_loss(logs-old_logs, advantage, config.clip_ratio)
        if not torch.isfinite(loss):
            raise RuntimeError('Nonfinite PPO loss')
        loss.backward()
        if any(p.grad is not None and not torch.isfinite(p.grad).all() for p in actor.parameters()):
            raise RuntimeError('Nonfinite PPO gradients')
        optimizer.step()
        with torch.no_grad():
            new_means = actor(states)
            finite = bool(torch.isfinite(new_means).all())
            kl = gaussian_kl(old_means, new_means, actor.exploration_std).mean().item() if finite else float('inf')
        max_attempted_kl = max(max_attempted_kl, kl)
        if not finite or kl > config.target_kl:
            actor.load_state_dict(parameters)
            optimizer.load_state_dict(adam)
            optimizer.zero_grad(set_to_none=True)
            rejected = True
            break
        accepted += 1
        loss_value = loss.item()
    if any(p.grad is not None for p in critic.parameters()):
        raise RuntimeError('Actor loss leaked into critic gradients')
    actor_before_critic = deepcopy(actor.state_dict())
    for _ in range(config.critic_steps):
        train_critic(critic, critic_optimizer, data['states_by_episode'], data['returns_by_episode'])
    if any(not torch.equal(value, actor.state_dict()[key]) for key,value in actor_before_critic.items()):
        raise RuntimeError('Critic update changed actor parameters')
    with torch.no_grad():
        means = actor(states)
        logs = torch.distributions.Normal(means, actor.exploration_std).log_prob(samples).sum(-1)
        return dict(actor_optimizer_steps=accepted, rejected_overshoot=rejected,
                    max_attempted_kl=max_attempted_kl, mean_kl=gaussian_kl(old_means, means, actor.exploration_std).mean().item(),
                    clip_fraction=((logs-old_logs).exp().sub(1).abs() > config.clip_ratio).float().mean().item(),
                    actor_loss=loss_value, critic_train=critic_metrics(critic, data))
