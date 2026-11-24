"""Bounded paired reward check; never resumes or overwrites a production model.

Both branches start from the same saved actor with fresh critics/Adam, matched
exploration streams and evaluation seeds. Returns across objectives are not
comparable; physical balanced duration is the common outcome. Three updates
cannot demonstrate convergence or robust recovery.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
from statistics import mean
import torch
from python.arena_env import checkpoint_environment, make_environment
from python.model_checkpoint import policy_from_checkpoint, save_checkpoint
from python.train_nn import TrainingConfig, create_critic, train_batch, evaluate_balance


def pilot(source, output, updates=3, episodes=32):
    if not 1 <= updates <= 3:
        raise ValueError('Diagnostic pilot is limited to three updates')
    payload = Path(source).read_bytes()
    data = torch.load(io.BytesIO(payload), map_location='cpu', weights_only=True)
    historical = checkpoint_environment(data)
    policy_from_checkpoint(data, historical)  # Validate source exactly before transfer.
    destination = Path(output); destination.mkdir(parents=True, exist_ok=False)
    torch.set_num_threads(1)
    cfg = TrainingConfig(collection_mode="historical", batch_size=episodes, agents=data.get('arena_agents', 1), batches=updates)
    result = dict(source=Path(source).name, sha256=hashlib.sha256(payload).hexdigest(),
                  config=vars(cfg), limitation='One actor; three updates; no recovery/generalization guarantee', branches=[])
    for version in (2, 3):
        env = make_environment(cfg.agents, spacing=data.get('arena_spacing', 12.), quality_version=version)
        actor = policy_from_checkpoint(data, historical)
        actor.environment_contract = env.environment_contract
        optimizer = torch.optim.Adam(actor.parameters(), lr=cfg.learning_rate)
        critic, critic_optimizer = create_critic(env, cfg)
        branch = dict(quality_version=version, environment_contract=env.environment_contract, measurements=[])
        result['branches'].append(branch)
        for update in range(updates + 1):
            training = None
            if update:
                torch.manual_seed(290041 + update)
                training = train_batch(env, actor, optimizer, episodes, critic=critic, critic_optimizer=critic_optimizer)
            deterministic = evaluate_balance(env, actor)
            stochastic = [evaluate_balance(env, actor, stochastic=True, seed=seed)
                          for seed in (190042, 190043, 190044)]
            row = dict(update=update, deterministic=deterministic, stochastic=stochastic,
                       stochastic_mean_balanced_seconds=mean(item['balanced_seconds'] for item in stochastic))
            if training:
                row['training'] = {key: training[key] for key in ('loss', 'critic_loss', 'max_parameter_change')}
            branch['measurements'].append(row)
            (destination/'results.json').write_text(json.dumps(result, indent=2)+'\n')
            print(f'quality {version} / update {update} / det balance {deterministic["balanced_seconds"]:.4f}s / '
                  f'stoch balance {row["stochastic_mean_balanced_seconds"]:.4f}s', flush=True)
        save_checkpoint(actor, optimizer, episodes*updates, destination/f'quality{version}_pilot.pt',
                        critic=critic, critic_optimizer=critic_optimizer, training_seed=cfg.seed)
    if hashlib.sha256(Path(source).read_bytes()).hexdigest() != result['sha256']:
        raise RuntimeError('Source changed during pilot; recorded frozen snapshot was used')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path); parser.add_argument('output', type=Path)
    parser.add_argument('--updates', type=int, default=3); parser.add_argument('--episodes', type=int, default=32)
    args = parser.parse_args(); pilot(args.source, args.output, args.updates, args.episodes)
