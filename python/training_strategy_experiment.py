"""Bounded diagnostics, MC/PPO comparison, then matched actor-capacity tests.

No production trainer, physics, reward, checkpoint or CLI default is replaced.
PPO here means PPO-Clip with normalized Monte Carlo advantages, full-batch Adam
and rollback on an empirical exact-KL overshoot. Critic remains 64x64 with one
fit step per batch so the capacity comparison changes only the actor width.
"""
import argparse
from copy import deepcopy
from dataclasses import asdict
import hashlib
import io
import json
from pathlib import Path
from statistics import mean
import time
import torch
import aura_python
from .arena_env import checkpoint_environment, make_environment
from .aura_policy import AuraPolicy, AuraValue
from .model_checkpoint import policy_from_checkpoint
from .ppo_experiment import PPOConfig, critic_metrics, policy_change, update_ppo
from .train_nn import evaluate_balance, train_batch


def freeze(path):
    payload = Path(path).read_bytes()
    return torch.load(io.BytesIO(payload), map_location='cpu', weights_only=True), hashlib.sha256(payload).hexdigest()


def evaluate(env, actor):
    deterministic = evaluate_balance(env, actor)
    stochastic = [evaluate_balance(env, actor, stochastic=True, seed=seed)
                  for seed in (190042,190043,190044)]
    return dict(deterministic=deterministic, stochastic=stochastic,
                stochastic_balanced_seconds=mean(v['balanced_seconds'] for v in stochastic),
                stochastic_reward=mean(v['total_reward'] for v in stochastic),
                stochastic_fall_rate=mean(v['fall_rate'] for v in stochastic))


def run(output, source, latest=None, *, updates=3, episodes=32, seeds=(42,43,44), continue_existing=False):
    if type(updates) is not int or not 1<=updates<=3 or episodes!=32:
        raise ValueError('Pilot requires 32 episodes/update and at most three updates')
    if not seeds or len(set(seeds))!=len(seeds) or any(type(s) is not int for s in seeds):
        raise ValueError('Distinct integer actor seeds required')
    destination=Path(output);destination.mkdir(parents=True,exist_ok=continue_existing)
    torch.set_num_threads(1)
    saved,digest=freeze(source)
    historical=checkpoint_environment(saved)
    policy_from_checkpoint(saved,historical)
    agents=saved.get('arena_agents',1)
    if agents!=4:
        raise ValueError('This matched protocol expects the saved four-agent arena')
    env=make_environment(agents,spacing=saved.get('arena_spacing',12.))
    config=PPOConfig()
    result=dict(protocol_version=2,source=dict(filename=Path(source).name,sha256=digest),
        environment_contract=env.environment_contract,simulator_sha256=hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest(),
        updates=updates,episodes_per_update=episodes,actor_seeds=list(seeds),ppo=asdict(config),
        diagnostic_actor_lr=.001,controlled_actor_lr=.0001,critic_lr=.001,critic_hidden_sizes=[64,64],gamma=.995,critic_return_scale=100.,
        heldout_episodes_per_update=8,evaluation_streams=[190042,190043,190044],
        checkpoint_diagnostics=[],branches=[],started=time.time())
    if continue_existing:
        previous=json.loads((destination/'results.json').read_text())
        for key in ('source','environment_contract','simulator_sha256','updates','episodes_per_update','actor_seeds'):
            if previous[key]!=result[key]:raise ValueError(f'Experiment continuation differs: {key}')
        result['started']=previous['started']
        result['checkpoint_diagnostics']=previous['checkpoint_diagnostics']
        result['branches']=[b for b in previous['branches'] if len(b['measurements'])==updates+1
                            and (destination/(b['label']+'.pt')).exists()]
        result['calibration']=previous.get('calibration',[])
        for b in previous['branches']:
            if b not in result['branches'] and previous.get('protocol_version')==1:
                result['calibration'].append(dict(reason='At .001 the first proposed PPO step exceeded the .01 KL bound; '
                    'choose .0001 based on update size, then test an MC rate control.',**b))
        for b in result['branches']:
            b.setdefault('actor_lr',.001)
    def persist():
        temporary=destination/'results.tmp'
        temporary.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
        temporary.replace(destination/'results.json')
    persist()
    # Historical critics must be judged against the objective they were trained on.
    for label,path in [('best',source)]+([('latest',latest)] if latest else []):
        if any(row['label']==label for row in result['checkpoint_diagnostics']):continue
        data,sha=freeze(path);replay=checkpoint_environment(data)
        actor=policy_from_checkpoint(data,replay)
        critic=AuraValue(replay.observation_size);critic.load_state_dict(data['critic']['model'])
        heldout=replay.collect_batch(actor,8,gradients=False,seed=490042)
        row=dict(label=label,filename=Path(path).name,sha256=sha,quality_version=replay.quality_version,
                 heldout=critic_metrics(critic,heldout))
        result['checkpoint_diagnostics'].append(row);persist()
        print(f'historical {label} critic: {row["heldout"]}',flush=True)

    def branch(label,algorithm,seed,width,warmstart=False,actor_lr=.0001):
        if any(row['label']==label for row in result['branches']):
            print(f'{label} / retained completed measurements',flush=True)
            return
        torch.manual_seed(seed)
        actor=(policy_from_checkpoint(saved,historical) if warmstart else
               AuraPolicy(env.observation_size,hidden_sizes=(width,width)))
        actor.environment_contract=env.environment_contract
        with torch.random.fork_rng(devices=[]):
            torch.manual_seed(seed+1);critic=AuraValue(env.observation_size)
        optimizer=torch.optim.Adam(actor.parameters(),lr=actor_lr)
        co=torch.optim.Adam(critic.parameters(),lr=.001)
        row=dict(label=label,algorithm=algorithm,actor_lr=actor_lr,actor_seed=seed,actor_hidden_sizes=[width,width],
                 warmstart=warmstart,actor_parameters=sum(p.numel() for p in actor.parameters()),measurements=[])
        result['branches'].append(row)
        initial_actor=deepcopy(actor.state_dict())
        row['measurements'].append(dict(update=0,evaluation=evaluate(env,actor)))
        persist();print(f'{label} / baseline / stochastic balance {row["measurements"][0]["evaluation"]["stochastic_balanced_seconds"]:.4f}s',flush=True)
        for update in range(1,updates+1):
            # Freeze validation trajectories before updating; never fit these episodes.
            heldout=env.collect_batch(actor,8,gradients=False,seed=390000+seed*100+update)
            validation_before=critic_metrics(critic,heldout)
            actor_before=deepcopy(actor)
            critic_before=deepcopy(critic.state_dict())
            training_seed=290000+seed*100+update
            if algorithm=='mc':
                torch.manual_seed(training_seed)
                batch=train_batch(env,actor,optimizer,episodes,critic=critic,critic_optimizer=co)
                optimization={key:batch[key] for key in ('loss','critic_loss','max_parameter_change')}
                optimization.update(actor_optimizer_steps=1,critic_train=critic_metrics(critic,batch))
            else:
                batch=env.collect_batch(actor,episodes,gradients=False,seed=training_seed,record_policy=True)
                optimization=update_ppo(actor,optimizer,critic,co,batch,config)
            measurements=dict(update=update,training_seed=training_seed,
                validation_seed=390000+seed*100+update,critic_heldout_before=validation_before,
                critic_heldout_after=critic_metrics(critic,heldout),optimization=optimization,
                policy_change=policy_change(actor_before,actor,batch['states_by_episode']),
                critic_max_parameter_change=max((value-critic.state_dict()[key]).abs().max().item()
                    for key,value in critic_before.items()),
                training_episode_steps=batch['episode_scores'],training_episode_returns=batch['episode_returns'],
                evaluation=evaluate(env,actor))
            row['measurements'].append(measurements);persist()
            print(f'{label} / update {update} / KL {measurements["policy_change"]["mean_kl"]:.5f} / '
                  f'heldout EV {measurements["critic_heldout_after"]["explained_variance"]:.3f} / '
                  f'stochastic balance {measurements["evaluation"]["stochastic_balanced_seconds"]:.4f}s',flush=True)
        torch.save(dict(kind='aura_strategy_experiment',format_version=1,production_resume=False,
            training_algorithm=algorithm,environment_contract=env.environment_contract,
            actor_hidden_sizes=[width,width],critic_hidden_sizes=[64,64],actor=actor.state_dict(),
            initial_actor=initial_actor,critic=critic.state_dict(),actor_optimizer=optimizer.state_dict(),
            critic_optimizer=co.state_dict(),rng_state=torch.get_rng_state(),seed=seed,
            updates=updates,episodes=updates*episodes,ppo=asdict(config)),destination/f'{label}.pt')

    branch('warmstart_mc','mc',42,64,True,actor_lr=.001)
    branch('warmstart_mc_small_step','mc',42,64,True)
    branch('warmstart_ppo','ppo',42,64,True)
    for seed in seeds:
        for width in (64,128):
            branch(f'capacity_{width}_seed{seed}','ppo',seed,width)
    result['duration_seconds']=time.time()-result['started']
    result['source_unchanged']=hashlib.sha256(Path(source).read_bytes()).hexdigest()==digest
    persist()
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--source',type=Path,default=Path('models/aura_balance_env5_best.pt'))
    parser.add_argument('--latest',type=Path,default=Path('models/aura_balance_env5.pt'))
    parser.add_argument('--continue',dest='continue_existing',action='store_true')
    args=parser.parse_args();run(args.output,args.source,args.latest,continue_existing=args.continue_existing)
