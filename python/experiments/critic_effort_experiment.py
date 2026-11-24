"""Matched one-versus-twenty critic fits with fixed 64x64 PPO actors.

Prior one-fit controls are frozen and validated, then their first batch is
replayed in each pair. Increased fitting happens AFTER the frozen-baseline actor
update, so its first possible influence on the actor is the next batch.
No production models, trainer defaults, physics or rewards are modified.
"""
import argparse
from copy import deepcopy
from dataclasses import asdict
import hashlib
import json
from math import isclose
from pathlib import Path
from statistics import mean
import time
import torch
import aura_python
from python.arena_env import checkpoint_environment, make_environment
from python.aura_policy import AuraPolicy, AuraValue
from python.model_checkpoint import policy_from_checkpoint
from python.ppo_experiment import PPOConfig, critic_metrics, policy_change, update_ppo
from python.training_strategy_experiment import evaluate, freeze


LABELS = ('warmstart_ppo','capacity_64_seed42','capacity_64_seed43','capacity_64_seed44')


def validate_reference(reference, env, source_hash, simulator_hash, manifest):
    expected = dict(source_hash=source_hash, simulator_hash=simulator_hash)
    if reference['source']['sha256'] != expected['source_hash']:
        raise ValueError('Reference source actor changed')
    if reference['environment_contract'] != env.environment_contract:
        raise ValueError('Reference environment contract changed')
    if reference['simulator_sha256'] != simulator_hash:
        raise ValueError('Reference simulator changed')
    if (reference.get('protocol_version') != 2 or 'duration_seconds' not in reference or
        reference['updates'] != 3 or reference['episodes_per_update'] != 32 or
        reference['critic_lr'] != .001 or reference['critic_hidden_sizes'] != [64,64] or
        reference['gamma'] != .995 or reference['critic_return_scale'] != 100. or
        reference['heldout_episodes_per_update'] != 8 or
        reference['evaluation_streams'] != [190042,190043,190044] or
        reference['ppo'] != asdict(PPOConfig())):
        raise ValueError('Reference optimization or sampling protocol differs')
    for name in ('python/arena_env.py','python/ppo_experiment.py','python/train_nn.py',
                 'python/aura_policy.py','python/balance_quality.py','python/training_strategy_experiment.py'):
        if hashlib.sha256(Path(name).read_bytes()).hexdigest()!=manifest['source_files'].get(name):
            raise ValueError(f'Reference implementation changed: {name}')
    controls={b['label']:b for b in reference['branches'] if b['label'] in LABELS}
    if set(controls)!=set(LABELS):raise ValueError('Missing matched one-fit controls')
    for label,branch in controls.items():
        seed=42 if label=='warmstart_ppo' else int(label.rsplit('seed',1)[1])
        if (branch['algorithm']!='ppo' or branch['actor_lr']!=.0001 or
            branch['actor_hidden_sizes']!=[64,64] or len(branch['measurements'])!=4 or
            branch['actor_seed']!=seed or branch['warmstart']!=(label=='warmstart_ppo')):
            raise ValueError(f'Invalid reference branch: {label}')
        for update,m in enumerate(branch['measurements']):
            if m['update']!=update:raise ValueError('Incomplete reference update sequence')
            if update and (m['training_seed']!=290000+branch['actor_seed']*100+update or
                           m['validation_seed']!=390000+branch['actor_seed']*100+update):
                raise ValueError('Reference exploration seeds changed')
    return controls


def assert_matches(actual, expected, path='measurement'):
    if isinstance(expected,dict):
        if set(actual)!=set(expected):raise ValueError(f'Replay fields differ: {path}')
        for key in expected:assert_matches(actual[key],expected[key],path+'.'+key)
    elif isinstance(expected,list):
        if len(actual)!=len(expected):raise ValueError(f'Replay length differs: {path}')
        for i,(a,b) in enumerate(zip(actual,expected)):assert_matches(a,b,f'{path}[{i}]')
    elif isinstance(expected,(float,int)) and not isinstance(expected,bool):
        if not isclose(actual,expected,rel_tol=1e-8,abs_tol=1e-9):
            raise ValueError(f'Replay value differs: {path}: {actual} != {expected}')
    elif actual!=expected:
        raise ValueError(f'Replay value differs: {path}')


def run(output, reference_dir, source):
    torch.set_num_threads(1)
    reference_dir=Path(reference_dir)
    reference_bytes=(reference_dir/'results.json').read_bytes()
    reference=json.loads(reference_bytes)
    manifest=json.loads((reference_dir/'manifest.json').read_text())
    saved,source_hash=freeze(source)
    historical=checkpoint_environment(saved)
    env=make_environment(saved.get('arena_agents',1),spacing=saved.get('arena_spacing',12.))
    simulator_hash=hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest()
    controls=validate_reference(reference,env,source_hash,simulator_hash,manifest)
    destination=Path(output);destination.mkdir(parents=True,exist_ok=False)
    (destination/'one-fit-reference.json').write_bytes(reference_bytes)
    config=PPOConfig(critic_steps=20)
    result=dict(protocol_version=1,source=dict(filename=Path(source).name,sha256=source_hash),
        reference=dict(directory=str(reference_dir),sha256=hashlib.sha256(reference_bytes).hexdigest()),
        environment_contract=env.environment_contract,simulator_sha256=simulator_hash,
        ppo=asdict(config),actor_hidden_sizes=[64,64],critic_hidden_sizes=[64,64],
        actor_lr=.0001,critic_lr=.001,episodes_per_update=32,updates=3,
        heldout_episodes=8,evaluation_streams=[190042,190043,190044],branches=[],started=time.time())
    def persist():
        temporary=destination/'results.tmp'
        temporary.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
        temporary.replace(destination/'results.json')
    persist()
    for label in LABELS:
        control=controls[label];seed=control['actor_seed']
        torch.manual_seed(seed)
        actor=(policy_from_checkpoint(saved,historical)
               if control['warmstart'] else AuraPolicy(205))
        actor.environment_contract=env.environment_contract
        initial_actor=deepcopy(actor.state_dict())
        with torch.random.fork_rng(devices=[]):
            torch.manual_seed(seed+1);critic=AuraValue(205)
        optimizer=torch.optim.Adam(actor.parameters(),lr=.0001)
        co=torch.optim.Adam(critic.parameters(),lr=.001)
        branch=dict(label=label,actor_seed=seed,warmstart=control['warmstart'],critic_steps=20,
                    one_fit_control=deepcopy(control),measurements=[])
        result['branches'].append(branch)
        baseline=evaluate(env,actor)
        assert_matches(baseline,control['measurements'][0]['evaluation'],'baseline')
        branch['measurements'].append(dict(update=0,evaluation=baseline))
        persist();print(f'{label} / 20 fits / baseline {baseline["stochastic_balanced_seconds"]:.4f}s',flush=True)
        for update in range(1,4):
            training_seed=290000+seed*100+update
            validation_seed=390000+seed*100+update
            validation=env.collect_batch(actor,8,gradients=False,seed=validation_seed)
            before_validation=critic_metrics(critic,validation)
            actor_before=deepcopy(actor)
            batch=env.collect_batch(actor,32,gradients=False,seed=training_seed,record_policy=True)
            replay=None
            if update==1:
                # Extra critic fits cannot influence this batch's detached actor baseline.
                control_actor,control_critic=deepcopy(actor),deepcopy(critic)
                ao=torch.optim.Adam(control_actor.parameters(),lr=.0001)
                vo=torch.optim.Adam(control_critic.parameters(),lr=.001)
                replay=update_ppo(control_actor,ao,control_critic,vo,batch,PPOConfig())
                original=control['measurements'][1]
                assert_matches(before_validation,original['critic_heldout_before'],'critic before replay')
                assert_matches(replay,original['optimization'],'one-fit optimization replay')
                assert_matches(critic_metrics(control_critic,validation),original['critic_heldout_after'],'critic after replay')
            optimization=update_ppo(actor,optimizer,critic,co,batch,config)
            evaluation=evaluate(env,actor)
            if update==1:
                if any(not torch.equal(value,actor.state_dict()[key]) for key,value in control_actor.state_dict().items()):
                    raise RuntimeError('Extra critic fits changed the first actor update')
                assert_matches(evaluation,control['measurements'][1]['evaluation'],'first actor evaluation replay')
                branch['first_batch_replay_verified']=True
            measurement=dict(update=update,training_seed=training_seed,validation_seed=validation_seed,
                critic_heldout_before=before_validation,critic_heldout_after=critic_metrics(critic,validation),
                optimization=optimization,policy_change=policy_change(actor_before,actor,batch['states_by_episode']),
                training_episode_steps=batch['episode_scores'],training_episode_returns=batch['episode_returns'],
                evaluation=evaluation)
            branch['measurements'].append(measurement);persist()
            ev=measurement['critic_heldout_after']['explained_variance']
            print(f'{label} / update {update} / heldout EV {ev:.3f} / '
                  f'KL {measurement["policy_change"]["mean_kl"]:.5f} / '
                  f'stochastic balance {evaluation["stochastic_balanced_seconds"]:.4f}s',flush=True)
        torch.save(dict(kind='aura_critic_effort_experiment',production_resume=False,
            environment_contract=env.environment_contract,actor=actor.state_dict(),initial_actor=initial_actor,
            critic=critic.state_dict(),actor_optimizer=optimizer.state_dict(),critic_optimizer=co.state_dict(),
            actor_seed=seed,ppo=asdict(config),source_sha256=source_hash,
            reference_sha256=result['reference']['sha256']),destination/f'{label}_20_fits.pt')
    result['duration_seconds']=time.time()-result['started']
    result['source_unchanged']=freeze(source)[1]==source_hash
    result['reference_unchanged']=hashlib.sha256((reference_dir/'results.json').read_bytes()).hexdigest()==result['reference']['sha256']
    persist()
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--reference',type=Path,default=Path('reports/training-strategy-20261016'))
    parser.add_argument('--source',type=Path,default=Path('models/aura_balance_env5_best.pt'))
    args=parser.parse_args();run(args.output,args.reference,args.source)
