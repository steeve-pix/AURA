"""Bounded integration check: one critic update, zero actor updates, no model files."""
import argparse
import json
from pathlib import Path
import torch

from python.train_nn import (TrainingConfig, create_training, create_critic, collect_critic_rollouts,
                       prepare_critic_data, train_critic, evaluate_balance)


def run():
    config = TrainingConfig(seed=42)
    env, actor, actor_optimizer = create_training(config)
    critic, critic_optimizer = create_critic(env, config)
    actor_before = [p.detach().clone() for p in actor.parameters()]
    critic_before = [p.detach().clone() for p in critic.parameters()]
    data = collect_critic_rollouts(env, actor, episodes=1, seed=130042)
    metrics = env.info['balance_metrics']
    if env.info['reward_total'] != 0. or not env.info['terminated']:
        raise AssertionError('Expected confirmed FALLEN with zero terminal reward')
    ticks = env.simulation.observation()['steps']
    try:
        env.step({})
    except ValueError:
        pass
    else:
        raise AssertionError('Rollout continued after FALLEN')
    assert env.simulation.observation()['steps'] == ticks
    states, targets = prepare_critic_data(critic, data['states_by_episode'], data['returns_by_episode'])
    assert states.shape[1] == 205 and torch.isfinite(states).all() and torch.isfinite(targets).all()
    mse_before = train_critic(critic, critic_optimizer, data['states_by_episode'], data['returns_by_episode'])
    with torch.no_grad():
        mse_after = torch.nn.functional.mse_loss(critic(states), targets).item()
    actor_change = max((p - old).abs().max().item() for p, old in zip(actor.parameters(), actor_before))
    critic_change = max((p - old).abs().max().item() for p, old in zip(critic.parameters(), critic_before))
    assert actor_change == 0. and not actor_optimizer.state and all(p.grad is None for p in actor.parameters())
    assert critic_change > 0.
    return dict(environment_contract=env.environment_contract, collection=metrics,
                deterministic=evaluate_balance(env, actor),
                stochastic=evaluate_balance(env, actor, stochastic=True, seed=10042),
                critic_mse_before=mse_before, critic_mse_after=mse_after,
                critic_max_parameter_change=critic_change, actor_max_parameter_change=actor_change,
                critic_optimizer_updates=1, actor_optimizer_updates=0,
                terminal_reward=0., post_terminal_guard=True, finite_inputs_and_targets=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    result = run()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open('x') as output:
        json.dump(result, output, indent=2, allow_nan=False)
        output.write('\n')
    print(json.dumps(result, indent=2))
