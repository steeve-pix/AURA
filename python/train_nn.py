"""Train AURA's balance policy with one Adam update per batch."""

from dataclasses import dataclass

import torch

DISCOUNT_FACTOR = 0.995
# Initial critic-training convention, independent of the actor advantage scale.
CRITIC_RETURN_SCALE = 100.0

if __package__:
    from .aura_env import AuraEnv
    from .aura_policy import AuraPolicy, outputs_to_action, CONTROLLED_ACTION_KEYS
    from .model_checkpoint import save_checkpoint
else:
    from aura_env import AuraEnv
    from aura_policy import AuraPolicy, outputs_to_action, CONTROLLED_ACTION_KEYS
    from model_checkpoint import save_checkpoint


@dataclass(frozen=True)
class TrainingConfig:
    batch_size: int = 8
    seed: int = 42
    learning_rate: float = 0.001

    def __post_init__(self):
        if not isinstance(self.batch_size, int) or self.batch_size < 2:
            raise ValueError("batch_size must be an integer of at least two")
        if not 0 < self.learning_rate < float("inf"):
            raise ValueError("learning_rate must be positive and finite")


def create_training(config):
    """The plain script and CLI use the same initialization."""
    torch.manual_seed(config.seed)
    env = AuraEnv()
    policy = AuraPolicy(env.observation_size)
    if tuple(policy.action_transforms) != CONTROLLED_ACTION_KEYS:
        raise ValueError("Policy action transforms must match the controlled-action schema")
    optimizer = torch.optim.Adam(policy.parameters(), lr=config.learning_rate)
    return env, policy, optimizer


def discounted_returns(rewards, gamma=DISCOUNT_FACTOR):
    """Return each step's discounted sum of current and future rewards."""
    returns = [0.0] * len(rewards)
    future_return = 0.0
    for i in reversed(range(len(rewards))):
        future_return = rewards[i] + gamma * future_return
        returns[i] = future_return
    return returns


def collect_critic_rollouts(env, policy, episodes, seed, progress=None, service=None):
    """Collect complete CPU-policy episodes without actor graphs or optimizer calls.

    A local exploration seed is isolated from the caller's RNG state. The
    observations are pre-action snapshots paired with raw reward-to-go.
    """
    if type(episodes) is not int or episodes < 1:
        raise ValueError("episodes must be a positive integer")
    if next(policy.parameters()).device.type != "cpu":
        raise ValueError("This controlled critic experiment collects CPU-policy rollouts")
    states_by_episode, returns_by_episode = [], []
    episode_scores, episode_returns = [], []
    was_training = policy.training
    policy.eval()
    try:
        with torch.random.fork_rng(devices=[]), torch.no_grad():
            torch.manual_seed(seed)
            for episode in range(episodes):
                if service is not None:
                    service()
                state, _ = env.reset()
                states, rewards = [], []
                while True:
                    if service is not None:
                        service()
                    states.append(state.detach().clone())
                    action, _ = policy.sample_action(state)
                    state, reward, terminated, truncated, info = env.step(action)
                    rewards.append(reward)
                    if terminated or truncated:
                        break
                returns = discounted_returns(rewards)
                assert len(states) == len(returns)
                states_by_episode.append(states)
                returns_by_episode.append(returns)
                episode_scores.append(info["steps"])
                episode_returns.append(sum(rewards))
                if progress is not None:
                    progress(episode+1, episodes, info["steps"])
    finally:
        policy.train(was_training)
    return {"states_by_episode": states_by_episode, "returns_by_episode": returns_by_episode,
            "episode_scores": episode_scores, "episode_returns": episode_returns}


def prepare_critic_data(critic, states_by_episode, returns_by_episode):
    """Build aligned detached inputs and targets in the critic's scaled units."""
    if not states_by_episode or len(states_by_episode) != len(returns_by_episode):
        raise ValueError("Critic data must contain matching, nonempty episode collections")
    flat_states, flat_returns = [], []
    for states, returns in zip(states_by_episode, returns_by_episode):
        if not states or len(states) != len(returns):
            raise ValueError("Each critic episode must contain matching, nonempty states and returns")
        if any(not isinstance(state, torch.Tensor) or state.ndim != 1 for state in states):
            raise ValueError("Critic states must be one-dimensional observation tensors")
        flat_states.extend(state.detach() for state in states)
        flat_returns.extend(returns)
    parameter = next(critic.parameters())
    try:
        states = torch.stack(flat_states).to(device=parameter.device, dtype=parameter.dtype)
        targets = torch.tensor(flat_returns, device=parameter.device, dtype=parameter.dtype)
    except (RuntimeError, TypeError, ValueError) as error:
        raise ValueError("Critic data must contain consistent observation sizes and numeric returns") from error
    if targets.ndim != 1:
        raise ValueError("Each critic target must be one scalar discounted return")
    targets = targets.reshape(-1, 1) / CRITIC_RETURN_SCALE
    if not torch.isfinite(states).all() or not torch.isfinite(targets).all():
        raise ValueError("Critic states and returns must be finite")
    return states, targets


def train_critic(critic, optimizer, states_by_episode, returns_by_episode):
    """Update only the critic once; return pre-update MSE in scaled-return units.

    Each pre-action state is paired with its raw discounted return. The critic
    predicts that return divided by CRITIC_RETURN_SCALE (initially 100.0), an
    experimental convention rather than a physically derived normalization.
    """
    states, targets = prepare_critic_data(critic, states_by_episode, returns_by_episode)

    optimizer.zero_grad()
    predictions = critic(states)
    if predictions.shape != targets.shape:
        raise ValueError("Critic predictions and targets must both have shape [N, 1]")
    loss = torch.nn.functional.mse_loss(predictions, targets)
    if not torch.isfinite(loss):
        raise RuntimeError("Non-finite critic loss; optimizer update skipped")
    loss.backward()
    if any(parameter.grad is not None and not torch.isfinite(parameter.grad).all()
           for parameter in critic.parameters()):
        raise RuntimeError("Non-finite critic gradient; optimizer update skipped")
    optimizer.step()
    return loss.item()


def train_batch(env, policy, optimizer, batch_size=None, update_parameters=True,
                compare_critic=None):
    """Collect a batch, measure its policy gradient, and optionally update once.

    compare_critic is diagnostic-only: retain the original gradient and also
    measure a detached critic-baseline gradient on the same sampled trajectories.
    Neither baseline changes the actor's existing return scale or loss reduction.
    """
    batch_size = TrainingConfig().batch_size if batch_size is None else batch_size
    if compare_critic is not None and update_parameters:
        raise ValueError("Critic-baseline comparison is diagnostic only; disable parameter updates")
    if not isinstance(batch_size, int) or batch_size < 2:
        raise ValueError("batch_size must be an integer of at least two")
    score_scale = env.episode_limit
    if not 0 < score_scale < float("inf"):
        raise ValueError("episode_limit must be positive and finite")
    episode_scores = []
    episode_log_probabilities = []
    episode_returns = []
    states_by_episode = []
    step_log_probs_by_episode = []
    returns_by_episode = []

    # Record survival, balance returns and the probabilities of sampled actions.
    for episode in range(batch_size):
        state, info = env.reset()
        episode_states = []
        log_probabilities = []
        rewards = []
        while True:
            episode_states.append(state.detach().clone())
            action, log_probability = policy.sample_action(state)
            state, reward, terminated, truncated, info = env.step(action)
            rewards.append(reward)
            log_probabilities.append(log_probability)
            if terminated or truncated:
                break

        assert len(episode_states) == len(rewards)
        assert len(episode_states) == len(log_probabilities)
        episode_scores.append(info["steps"])
        episode_returns.append(sum(rewards))
        states_by_episode.append(episode_states)
        episode_log_probabilities.append(torch.stack(log_probabilities).sum())
        step_log_probs_by_episode.append(log_probabilities)
        returns = discounted_returns(rewards)
        assert len(episode_states) == len(returns)
        returns_by_episode.append(returns)

    assert len(states_by_episode) == len(returns_by_episode)

    # Compare each balance return with the mean return of the other episodes.
    total_return = sum(episode_returns)
    advantages = [
        (episode_return - (total_return - episode_return) / (len(episode_returns) - 1)) / score_scale
        for episode_return in episode_returns
    ]
    # Episode advantages above remain diagnostics; gradients use reward-to-go.
    policy_terms = []
    absolute_advantages = []
    peerless_absolute_advantage = 0.0
    peerless_terms = 0
    for episode_index, log_probs in enumerate(step_log_probs_by_episode):
        returns = returns_by_episode[episode_index]
        for step, log_prob in enumerate(log_probs):
            peers = [
                other_returns[step]
                for index, other_returns in enumerate(returns_by_episode)
                if index != episode_index and step < len(other_returns)
            ]
            baseline = sum(peers) / len(peers) if peers else 0.0
            advantage = (returns[step] - baseline) / score_scale
            absolute_advantage = abs(advantage)
            absolute_advantages.append(absolute_advantage)
            if not peers:
                peerless_terms += 1
                peerless_absolute_advantage += absolute_advantage
            policy_terms.append(log_prob * advantage)
    loss = -torch.stack(policy_terms).sum() / len(step_log_probs_by_episode)

    # Backpropagation computes gradients; Adam uses them to update weights and biases.
    optimizer.zero_grad()
    parameters_before = [parameter.detach().clone() for parameter in policy.parameters()]
    critic_comparison = None
    if compare_critic is not None:
        # Reuse the exact sampled-action log probabilities, including latent
        # Gaussian samples that were clipped before execution. No resampling.
        states, targets = prepare_critic_data(compare_critic, states_by_episode, returns_by_episode)
        was_training = compare_critic.training
        compare_critic.eval()
        try:
            with torch.no_grad():
                predicted = compare_critic(states)
                if predicted.shape != targets.shape:
                    raise ValueError("Critic predictions must have shape [N, 1]")
                # (G/100 - V) * 100/episode_limit: same actor scale as LOO.
                critic_advantages = ((targets - predicted) * (CRITIC_RETURN_SCALE / score_scale)).flatten()
        finally:
            compare_critic.train(was_training)
        if not torch.isfinite(critic_advantages).all():
            raise ValueError("Critic advantages must be finite")
        flat_log_probs = torch.stack([value for episode in step_log_probs_by_episode for value in episode])
        critic_loss = -(flat_log_probs * critic_advantages.to(flat_log_probs)).sum() / batch_size
        gradients = torch.autograd.grad(critic_loss, tuple(policy.parameters()), retain_graph=True)
        critic_gradient = torch.cat([gradient.detach().flatten() for gradient in gradients])
        if not torch.isfinite(critic_gradient).all():
            raise RuntimeError("Non-finite critic-baseline policy gradient")
        critic_comparison = {"critic_loss": critic_loss.item(), "critic_gradient": critic_gradient}
    loss.backward()
    gradients_are_finite = all(
        torch.isfinite(parameter.grad).all() for parameter in policy.parameters()
    )
    if not torch.isfinite(loss) or not gradients_are_finite:
        raise RuntimeError("Non-finite loss or gradient; Adam update skipped.")
    if update_parameters:
        optimizer.step()

    max_parameter_change = max(
        (parameter.detach() - before).abs().max().item()
        for parameter, before in zip(policy.parameters(), parameters_before)
    )
    total_absolute_advantage = sum(absolute_advantages)
    result = {
        "episode_scores": episode_scores,
        "episode_returns": episode_returns,
        "states_by_episode": states_by_episode,
        # Raw reward-to-go at each saved pre-action state; no actor scaling.
        "returns_by_episode": returns_by_episode,
        "advantages": advantages,
        "episode_log_probabilities": [value.item() for value in episode_log_probabilities],
        "loss": loss.item(),
        "max_parameter_change": max_parameter_change,
        "action_timestep_terms": len(absolute_advantages),
        "peerless_terms": peerless_terms,
        "average_absolute_advantage": total_absolute_advantage / len(absolute_advantages),
        "maximum_absolute_advantage": max(absolute_advantages),
        "peerless_absolute_advantage_percent": (
            100.0 * peerless_absolute_advantage / total_absolute_advantage
            if total_absolute_advantage > 0.0 else 0.0
        ),
        "parameters_updated": update_parameters,
    }
    if critic_comparison is not None:
        result["gradient_comparison"] = {
            **critic_comparison,
            "original_gradient": torch.cat([parameter.grad.detach().flatten().clone()
                                             for parameter in policy.parameters()]),
            "actor_advantage_scale": score_scale,
            "critic_return_scale": CRITIC_RETURN_SCALE,
        }
    return result


def evaluate_policy(env, policy):
    """Measure survival without exploration or learning."""
    was_training = policy.training
    policy.eval()
    try:
        state, info = env.reset()
        with torch.inference_mode():
            while not info["done"]:
                output = policy(state)
                action = outputs_to_action(
                    output.clamp(*policy.sample_bounds),
                    policy.action_transforms,
                )
                state, _, terminated, truncated, info = env.step(action)
                if terminated or truncated:
                    break
        return info["steps"]
    finally:
        policy.train(was_training)


def main():
    config = TrainingConfig()
    env, policy, optimizer = create_training(config)
    result = train_batch(env, policy, optimizer, config.batch_size)
    checkpoint = save_checkpoint(policy, optimizer, len(result["episode_scores"]))
    for score, total_reward in zip(result["episode_scores"], result["episode_returns"]):
        print(f"Survival: {score} | Balance return: {total_reward:.2f}")
    print(f"Maximum weight/bias change: {result['max_parameter_change']:.8f}")
    print("Training loss:", result["loss"])
    print("Policy weights and biases updated.")
    print(f"Checkpoint saved: {checkpoint}")


if __name__ == "__main__":
    main()
