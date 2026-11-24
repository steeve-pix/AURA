"""Read-only, per-episode reward-factor measurements on matched physical rollouts.

No optimizer is constructed or stepped. Existing rewards, physics, checkpoints
and the running trainer are unchanged. Factor-removal deltas are arithmetic
counterfactuals on these same states, not predicted policy improvements.
"""
import argparse
import csv
import json
from math import acos, cos, degrees, hypot, log, prod, radians
from pathlib import Path
from statistics import mean

import torch
from cli.backend.model_tools import snapshot
from python.arena_env import checkpoint_environment
from python.aura_policy import AuraPolicy, outputs_to_action
from python.model_checkpoint import policy_from_checkpoint
from python.balance_quality import balance_quality_components

FACTORS = ('posture', 'height', 'com_motion', 'body_rotation', 'support', 'stationary_hold', 'foot_grip')


def summarize(records):
    if not records:
        return dict(steps=0)
    components = {key: mean(row[key] for row in records) for key in FACTORS}
    quality = mean(row['balance_quality'] for row in records)
    grounded = [row for row in records if row['grounded']]
    floor = [row for row in grounded if row['support'] <= .25 + 1e-12]
    failures={key:sum(row.get('entry_failures',{}).get(key,False) for row in records) for key in
        ('pelvis_tilt','torso_tilt','height','vertical_speed','horizontal_speed','angular_speed','support')}
    return dict(steps=len(records), mean_components=components,
        entry_envelope_failure_counts=failures,
        mean_quality_with_hold_factors_always_on=mean(row['always_hold_quality'] for row in records),
        mean_quality_gate_exemption=mean(row['balance_quality']-row['always_hold_quality'] for row in records),
        mean_quality=quality, mean_reward=mean(row['reward'] for row in records),
        total_reward=sum(row['reward'] for row in records),
        component_below_point_one_fraction={key: mean(row[key]<.1 for row in records) for key in FACTORS},
        mean_negative_log={key: mean(-log(max(row[key],1e-12)) for row in records) for key in FACTORS},
        factor_removal_quality_delta={key: mean(prod(row[k] for k in FACTORS if k!=key) for row in records)-quality for key in FACTORS},
        grounded_steps=len(grounded), support_floor_steps=len(floor),
        support_floor_fraction_of_grounded=len(floor)/len(grounded) if grounded else None,
        floor_with_valid_polygon=sum(row['polygon_vertices']>=3 for row in floor),
        floor_with_degenerate_polygon=sum(row['polygon_vertices']<3 for row in floor),
        outside_by_point_one_steps=sum(row['margin']<=-.1 for row in grounded if row['polygon_vertices']>=3),
        mean_pelvis_tilt_degrees=mean(row['pelvis_tilt'] for row in records),
        mean_torso_tilt_degrees=mean(row['torso_tilt'] for row in records),
        mean_horizontal_com_speed=mean(row['horizontal_speed'] for row in records))


def record(info, reward):
    qualities = info['reward_qualities']
    from python.balance_quality import quality_implementation
    _, components = quality_implementation(info.get('quality_version', 2))
    always_hold=components(dict(info,balance_state='BALANCED'))
    refs=info['neutral_height_references']
    # Read-only copies of current BalanceConfig.enter thresholds; no classifier changes.
    entry_failures=dict(pelvis_tilt=info['pelvis_uprightness']<cos(radians(15)),
        torso_tilt=info['torso_uprightness']<cos(radians(20)),
        height=any(info[name+'_height']/refs[name]<.8 for name in ('pelvis','torso')) or
               info['center_of_mass_height']/refs['center_of_mass']<.8,
        vertical_speed=abs(info['center_of_mass_velocity'][1])>.35,
        horizontal_speed=hypot(info['center_of_mass_velocity'][0],info['center_of_mass_velocity'][2])>.35,
        angular_speed=any(hypot(*info[name+'_angular_velocity'])>.4 for name in ('pelvis','torso')),
        support=not (info['left_foot_grounded'] or info['right_foot_grounded']) or
                len(info['support_polygon'])<3 or info['support_margin']<0)
    row = dict(always_hold_quality=always_hold['balance_quality'],entry_failures=entry_failures,steps=info['steps'], state=info['balance_state'], reward=reward,
        grounded=bool(info['left_foot_grounded'] or info['right_foot_grounded']),
        polygon_vertices=len(info['support_polygon']), margin=info['support_margin'],
        pelvis_tilt=degrees(acos(max(-1.,min(1.,info['pelvis_uprightness'])))),
        torso_tilt=degrees(acos(max(-1.,min(1.,info['torso_uprightness'])))),
        horizontal_speed=hypot(info['center_of_mass_velocity'][0],info['center_of_mass_velocity'][2]),
        terminated=bool(info['terminated']), truncated=bool(info['truncated']), **qualities)
    return row


def rollout(env, policy, stochastic, seed):
    records = {}
    if getattr(env,'is_arena',False):
        def progress(episode,agent,info,action,reward,total):
            records.setdefault(episode,[]).append(record(info,reward))
        result=env.collect_batch(policy,env.agents,gradients=False,stochastic=stochastic,seed=seed,progress=progress)
        metrics=result['balance_metrics']
    else:
        with torch.random.fork_rng(devices=[]),torch.no_grad():
            torch.manual_seed(seed)
            state,info=env.reset()
            while not info['done']:
                action=policy.sample_action(state)[0] if stochastic else outputs_to_action(
                    policy(state).clamp(*policy.sample_bounds),policy.action_transforms)
                state,reward,_,_,info=env.step(action)
                records.setdefault(0,[]).append(record(info,reward))
            metrics=[info['balance_metrics']]
    episodes=[]
    for index,rows in sorted(records.items()):
        assert len(rows)==metrics[index]['steps']
        assert abs(sum(row['reward'] for row in rows)-metrics[index]['total_reward'])<1e-8
        episodes.append(dict(agent=index+1,metrics=metrics[index],summary=summarize(rows),
            by_state={state:summarize([r for r in rows if r['state']==state]) for state in ('BALANCED','UNBALANCED','FALLEN')},
            by_contact={name:summarize([r for r in rows if (not r['grounded'] if name=='airborne' else
                r['grounded'] and (r['support']<=.25+1e-12 if name=='support_floor' else r['support']>.25+1e-12))])
                for name in ('airborne','support_floor','above_floor')},records=rows))
    return episodes


def audit(path, output, seeds=(190042,190043,190044)):
    data,digest=snapshot(path)  # Freeze latest once, even if its trainer keeps saving.
    output=Path(output);output.mkdir(parents=True,exist_ok=False)
    with (output/'snapshot.pt').open('xb') as handle: torch.save(data,handle)
    env=checkpoint_environment(data)
    policy_from_checkpoint(data,env)
    if 'initial_actor' in data:
        initial=dict(data,model=data['initial_actor']); initial_label='Saved initial actor'
    else:
        if data.get('training_seed') is None: raise ValueError('No saved initial actor or initialization seed')
        with torch.random.fork_rng():
            torch.manual_seed(data['training_seed'])
            actor=AuraPolicy(env.observation_size,exploration_std=data['exploration_std'])
        initial=dict(data,model=actor.state_dict()); initial_label=f"Reconstructed seed {data['training_seed']} (original weights unavailable)"
    variants=[('Initial',initial,initial_label),('Latest',data,f"Frozen update {data.get('updates_completed',0)} / SHA-256 {digest}")]
    best_path=path.with_name(path.stem+'_best.pt')
    best_hash=None
    if best_path.exists():
        best,best_hash=snapshot(best_path)
        with (output/'snapshot_best.pt').open('xb') as handle: torch.save(best,handle)
        if best.get('training_run_id')==data.get('training_run_id'):
            variants.insert(1,('Best',best,f"Saved update {best.get('updates_completed',0)} / SHA-256 {best_hash}"))
    all_results=[]
    for label,saved,provenance in variants:
        policy=policy_from_checkpoint(saved,env);policy.eval()
        parameters={key:value.clone() for key,value in policy.state_dict().items()}
        episodes=[]
        for stochastic,seed in [(False,10042),*((True,seed) for seed in seeds)]:
            print(label,'stochastic' if stochastic else 'deterministic',seed,flush=True)
            for episode in rollout(env,policy,stochastic,seed):
                episodes.append(dict(stochastic=stochastic,exploration_seed=seed if stochastic else None,**episode))
        assert all(torch.equal(parameters[k],v) for k,v in policy.state_dict().items())
        groups={}
        for name,selected in [('deterministic',[e for e in episodes if not e['stochastic']]),
                              ('stochastic',[e for e in episodes if e['stochastic']])]:
            rows=[r for episode in selected for r in episode['records']]
            groups[name]=dict(summary=summarize(rows),
                mean_episode_survival=mean(e['metrics']['steps'] for e in selected),
                mean_episode_reward=mean(e['metrics']['total_reward'] for e in selected),
                mean_episode_balanced_seconds=mean(e['metrics']['balanced_seconds'] for e in selected),
                high_quality_unbalanced=summarize([r for r in rows if r['state']=='UNBALANCED' and r['balance_quality']>=.5]),
                by_state={state:summarize([r for r in rows if r['state']==state]) for state in ('BALANCED','UNBALANCED','FALLEN')},
                by_contact={name:summarize([r for r in rows if (not r['grounded'] if name=='airborne' else
                    r['grounded'] and (r['support']<=.25+1e-12 if name=='support_floor' else r['support']>.25+1e-12))])
                    for name in ('airborne','support_floor','above_floor')})
        all_results.append(dict(label=label,provenance=provenance,groups=groups,episodes=episodes))
    payload=dict(version=1,checkpoint=str(path.resolve()),latest_update=data.get('updates_completed'),
        latest_sha256=digest,best_sha256=best_hash,device='cpu',seeds=list(seeds),
        environment_contract=env.environment_contract,assumptions=['No reward coefficients changed.',
            'Initial actor reconstruction is labeled; historical initialization implementation is not independently archived.',
            'Component means and pooled quality are timestep-weighted; episode survival/reward/balance are episode-weighted.',
            'Counterfactual factor removal is arithmetic on recorded states, not a learning or causal experiment.',
            'Support-floor comparisons use the current configured 0.25 floor.',
            'Always-on hold factors only recompute reward on these states; no changed trajectory or classification is simulated.',
            'Entry-envelope checks copy current BalanceConfig.enter thresholds and do not model temporal hysteresis.',
            'Three exploration streams at one actor seed cannot establish generalization.'],results=all_results)
    with (output/'components.json').open('x') as handle:json.dump(payload,handle,indent=2,allow_nan=False)
    columns=['policy','stochastic','exploration_seed','agent','steps','balanced_seconds','reward',*FACTORS,'mean_quality','support_floor_fraction_of_grounded']
    with (output/'episodes.csv').open('x',newline='') as handle:
        writer=csv.DictWriter(handle,fieldnames=columns);writer.writeheader()
        for result in all_results:
            for e in result['episodes']:
                writer.writerow(dict(policy=result['label'],stochastic=e['stochastic'],exploration_seed=e['exploration_seed'],
                    agent=e['agent'],steps=e['metrics']['steps'],balanced_seconds=e['metrics']['balanced_seconds'],reward=e['metrics']['total_reward'],
                    **e['summary']['mean_components'],mean_quality=e['summary']['mean_quality'],
                    support_floor_fraction_of_grounded=e['summary']['support_floor_fraction_of_grounded']))
    return payload


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkpoint',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    audit(args.checkpoint,args.output)


if __name__=='__main__':main()
