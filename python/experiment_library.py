"""Versioned, bounded experiment execution and durable provenance.

No expression evaluation, arbitrary code plugins or implicit checkpoint resume.
Each training branch owns fresh optimizers and critic; source actors are read-only.
"""
from copy import deepcopy
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
from statistics import mean, stdev
import time
import traceback
from uuid import uuid4
import torch
from .model_templates import NetworkDesign, atomic_json, template, save_template, TEMPLATE_DIR
from .worker_config import default_workers,validate_workers,hardware_capacity,worker_warning
from .experiment_rewards import FORMULAS, reward_settings, apply_reward
from .independent_env import IndependentEnv,batch_seed,EPISODE_SEED_STRIDE,SEED_MODULUS
from .aura_policy import AuraPolicy,ACTION_SCHEMA_VERSION
from .aura_env import OBSERVATION_SIZE
from .train_nn import TrainingConfig,create_critic,train_batch,train_critic,resolve_training_device
from .model_checkpoint import save_checkpoint,policy_from_checkpoint
from .arena_env import checkpoint_environment
from .balance_quality import BalanceQualityConfig

ROOT=Path(__file__).resolve().parents[1]
LIBRARY=ROOT/'models/experiments'
KINDS={'baseline':'Frozen deterministic and stochastic evaluation',
       'training':'Matched actor/critic learning comparisons',
       'critic_fit':'Fresh critic fitting and held-out accuracy / actor frozen',
       'reward_audit':'Physical reward-component measurements, no optimization',
       'benchmark':'Independent collection throughput, no optimization'}
ALGORITHMS=('actor_critic_mc','ppo_clip')
COMPARISONS=('none','actor_hidden','critic_hidden','actor_activation','critic_activation',
             'actor_lr','critic_lr','episodes','workers','algorithm','critic_fits','reward_formula','reward_parameters','initialization','initialization_mode','source')
COMPARISON_FIELDS={
    'training':tuple(key for key in COMPARISONS if key!='source'),
    'baseline':('none','actor_hidden','actor_activation','initialization','initialization_mode','reward_formula','reward_parameters','workers'),
    'critic_fit':('none','actor_hidden','critic_hidden','actor_activation','critic_activation','critic_lr','episodes','workers','critic_fits','reward_formula','reward_parameters','initialization','initialization_mode'),
    'reward_audit':('none','source','actor_hidden','actor_activation','episodes','workers','reward_formula','reward_parameters','initialization','initialization_mode'),
    'benchmark':('none','actor_hidden','actor_activation','episodes','workers','initialization','initialization_mode')}


def inactive_fields(kind):
    inactive={'actor_lr','critic_lr','updates','algorithm','critic_fits'} if kind not in ('training','critic_fit') else set()
    if kind=='critic_fit':inactive={'actor_lr','algorithm'}
    return inactive

METRICS=('balanced_seconds','balanced_fraction','longest_balanced_steps','recoveries',
         'average_balance_quality','total_reward','steps','fall_rate')


def capabilities():
    from .model_templates import ACTIVATIONS,INITIALIZATIONS,MAX_HIDDEN_LAYERS,MAX_NEURONS
    return dict(schema_version=1,kinds=KINDS,algorithms=ALGORITHMS,comparisons=COMPARISONS,comparisons_by_kind=COMPARISON_FIELDS,
                activations=list(ACTIVATIONS),initializations=INITIALIZATIONS,hidden_layer_limit=MAX_HIDDEN_LAYERS,neurons_per_layer_limit=MAX_NEURONS,reward_formulas=FORMULAS,
                reward_parameters=asdict(BalanceQualityConfig()),starting_conditions=['neutral'],
                unsupported=['initial tilt: simulation reset has no configurable posture API',
                             'arbitrary expressions, architectures and algorithms'],
                hardware=dict(cpu_cores=hardware_capacity(),default_workers=default_workers()))


def configuration(name='experiment'):
    return dict(schema_version=1,name=name,kind='baseline',source='',initialization_mode='fresh',
        actor_hidden=[64,64],critic_hidden=[64,64],actor_activation='Tanh',critic_activation='Tanh',
        initialization='default',device='cpu',actor_lr=.001,critic_lr=.001,episodes=32,updates=3,workers=default_workers(),
        algorithm='actor_critic_mc',critic_fits=1,reward_formula='product',reward_parameters={},
        evaluation_seeds=[10042,10043,10044],training_seeds=[42,43,44],starting_condition='neutral',
        comparison='none',values=[],criteria=dict(min_balanced_seconds=1.,max_fall_rate=0.,minimum_seeds=3))


def validate(config):
    if config.get('schema_version')!=1: raise ValueError('Unsupported experiment schema')
    unknown=set(config)-set(configuration())
    if unknown: raise ValueError('Unknown experiment settings: '+', '.join(sorted(unknown)))
    data=configuration();data.update(deepcopy(config))
    template(data['name'])
    if data['kind'] not in KINDS or data['algorithm'] not in ALGORITHMS: raise ValueError('Unsupported experiment kind or algorithm')
    if data['device'] not in ('cpu','gpu'): raise ValueError('Choose CPU or GPU learning device')
    if data['starting_condition']!='neutral': raise ValueError('Only the existing neutral reset is supported; tilt API is not implemented')
    if data['initialization_mode'] not in ('fresh','transfer'): raise ValueError('Select fresh initialization or actor transfer')
    for prefix in ('actor','critic'):
        NetworkDesign(tuple(data[prefix+'_hidden']),data[prefix+'_activation'],data['initialization'])
        rate=data[prefix+'_lr']
        if not isinstance(rate,(int,float)) or not math.isfinite(rate) or rate<=0: raise ValueError('Learning rates must be positive and finite')
    validate_workers(data['workers'])
    for key,lo,hi in (('episodes',2,128),('updates',1,1000),('critic_fits',1,100)):
        if type(data[key]) is not int or not lo<=data[key]<=hi: raise ValueError(f'{key}: supported range {lo}–{hi}')
    for key in ('evaluation_seeds','training_seeds'):
        values=data[key]
        if not isinstance(values,list) or not 1<=len(values)<=32 or any(type(n) is not int or not 0<=n<2**63-1 for n in values) or len(set(values))!=len(values):
            raise ValueError('Use 1–32 distinct nonnegative integer seeds')
    if set(data['evaluation_seeds']) & set(data['training_seeds']): raise ValueError('Training and evaluation seeds must be separate')
    if data['kind'] in ('training','critic_fit'):
        for seed in data['training_seeds']:
            for update in range(data['updates']):
                start=batch_seed(seed,update)
                for evaluated in data['evaluation_seeds']:
                    delta=(evaluated-start)%SEED_MODULUS
                    if delta%EPISODE_SEED_STRIDE==0 and delta//EPISODE_SEED_STRIDE<data['episodes']:
                        raise ValueError('Evaluation seed overlaps an assigned training episode; select separate evaluation seeds')
    if 'angular_dynamic_fraction' in data['reward_parameters']: raise ValueError('angular_dynamic_fraction is an inactive historical reference in Quality 3')
    reward_settings(3,data['reward_formula'],data['reward_parameters'])
    if data['comparison'] not in COMPARISON_FIELDS[data['kind']]: raise ValueError('Unsupported comparison field')
    if data['comparison']=='source':
        if not data['source'] or data['initialization_mode']!='transfer':
            raise ValueError('Comparing actors requires a control checkpoint and transfer weights')
        paths=[data['source']]+data['values']
        if any(not isinstance(value,str) or not value for value in paths):
            raise ValueError('Comparison values must be a list of checkpoint paths')
        if len({str(Path(value).resolve()) for value in paths})!=len(paths):
            raise ValueError('Select distinct actor checkpoints for comparison')
    limit=287 if data['comparison']=='source' else 8
    if data['comparison']!='none' and not 1<=len(data['values'])<=limit: raise ValueError(f'Use 1–{limit} comparison values')
    for value in data['values'] if data['comparison']!='none' else []:
        branch=deepcopy(data);branch[data['comparison']]=value;branch.update(comparison='none',values=[]);validate(branch)
    criteria=data['criteria']
    if set(criteria)!= {'min_balanced_seconds','max_fall_rate','minimum_seeds'} or not 0<=criteria['min_balanced_seconds']<=10 or not 0<=criteria['max_fall_rate']<=1 or type(criteria['minimum_seeds']) is not int or not 1<=criteria['minimum_seeds']<=32:
        raise ValueError('Invalid predeclared physical validation criteria')
    if data['source'] and not Path(data['source']).is_file(): raise ValueError('Source checkpoint does not exist')
    if data['source']:
        saved=torch.load(data['source'],map_location='cpu',weights_only=True)
        if data['kind']=='baseline' and len(data['training_seeds'])!=1:
            raise ValueError('A frozen source baseline has one actor; use one training seed and multiple evaluation seeds')
        if data['kind'] not in ('training','critic_fit') and data['initialization_mode']!='transfer':
            raise ValueError('A source checkpoint requires saved-weight transfer for frozen studies; clear source for fresh initialization')
        if data['initialization_mode']=='transfer' and data['kind'] in ('training','critic_fit'):
            sizes=[layer['outputs'] for layer in saved.get('layers',[]) if layer['type']=='Linear'][:-1]
            activations=[layer['type'] for layer in saved.get('layers',[]) if layer['type']!='Linear'][:-1]
            if sizes!=list(data['actor_hidden']) or any(a!=data['actor_activation'] for a in activations):
                raise ValueError('Changing actor architecture requires explicit fresh initialization')
    return data


def branches(config):
    yield 'control',dict(config,comparison='none',values=[])
    if config['comparison']!='none':
        for i,value in enumerate(config['values']):
            branch=deepcopy(config);branch[config['comparison']]=value;branch.update(comparison='none',values=[])
            yield f'variant_{i+1}',branch


def _snapshot_compared_sources(destination,config):
    """Freeze additional actors; unchanged draft sources retain their reviewed bytes."""
    manifest=destination/'compared_sources.json'
    if config['comparison']!='source':
        manifest.unlink(missing_ok=True)
        return
    previous=json.loads(manifest.read_text()) if manifest.exists() else []
    snapshots=[]
    for index,source in enumerate(config['values'] if config['comparison']=='source' else []):
        saved=next((item for item in previous if item['path']==source),None)
        if saved is None:
            payload=Path(source).read_bytes()
            filename=f'compared_source_{uuid4().hex}.pt'
            (destination/filename).write_bytes(payload)
            saved=dict(path=source,snapshot=filename,sha256=hashlib.sha256(payload).hexdigest())
        snapshots.append(saved)
    atomic_json(manifest,snapshots)


def create_experiment(config, directory=LIBRARY):
    config=validate(config)
    destination=Path(directory)/(config['name']+'-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')+'-'+uuid4().hex[:8])
    destination.mkdir(parents=True,exist_ok=False)
    if config['source']:
        payload=Path(config['source']).read_bytes()
        (destination/'source_snapshot.pt').write_bytes(payload)
        atomic_json(destination/'source.json',dict(path=config['source'],sha256=hashlib.sha256(payload).hexdigest()))
    _snapshot_compared_sources(destination,config)
    atomic_json(destination/'configuration.json',config)
    atomic_json(destination/'state.json',dict(status='draft',created_utc=datetime.now(timezone.utc).isoformat()))
    return destination


def update_experiment(path, config):
    """Edit an unrun draft while retaining its identity and source snapshot."""
    import fcntl
    path=Path(path)
    config=validate(config)
    with (path/'.run.lock').open('a') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError as exc:raise ValueError('Experiment is already running') from exc
        state=json.loads((path/'state.json').read_text())
        if state['status']!='draft':
            raise ValueError('Only drafts can be edited; copy this experiment for a new run')
        original=json.loads((path/'configuration.json').read_text())
        if config['source']!=original['source']:
            if config['source']:
                payload=Path(config['source']).read_bytes()
                temporary=path/'source_snapshot.pt.tmp'
                temporary.write_bytes(payload)
                temporary.replace(path/'source_snapshot.pt')
                atomic_json(path/'source.json',dict(path=config['source'],sha256=hashlib.sha256(payload).hexdigest()))
            else:
                (path/'source_snapshot.pt').unlink(missing_ok=True)
                (path/'source.json').unlink(missing_ok=True)
        _snapshot_compared_sources(path,config)
        atomic_json(path/'configuration.json',config)
    return path


def rename_experiment(path, name):
    """Change a display label without replacing the run identity or its evidence."""
    import fcntl
    template(name)  # Use the same name rules as experiment creation.
    path=Path(path)
    with (path/'.run.lock').open('a') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError as exc:raise ValueError('Wait for this experiment to finish before renaming it') from exc
        config=json.loads((path/'configuration.json').read_text())
        config['name']=name
        atomic_json(path/'configuration.json',config)
    return path


def library(directory=LIBRARY):
    rows=[]
    for folder in sorted(Path(directory).glob('*'),reverse=True):
        if not folder.is_dir(): continue
        try:
            config=json.loads((folder/'configuration.json').read_text());state=json.loads((folder/'state.json').read_text())
            rows.append(dict(path=str(folder),name=config['name'],kind=config['kind'],status=state['status'],configuration=config))
        except (OSError,ValueError,KeyError):
            rows.append(dict(path=str(folder),name=folder.name,kind='unknown',status='invalid'))
    return rows


def _source(config,env,seed,device):
    torch.manual_seed(seed)
    if config['source'] and (config['kind'] in ('baseline','reward_audit','benchmark') or config['initialization_mode']=='transfer'):
        data=torch.load(config['source'],map_location='cpu',weights_only=True)
        historical=checkpoint_environment(data)
        actor=policy_from_checkpoint(data,historical)
        if config['kind'] in ('training','critic_fit'):
            saved=[layer['outputs'] for layer in data['layers'] if layer['type']=='Linear'][:-1]
            activations=[layer['type'] for layer in data['layers'] if layer['type']!='Linear'][:-1]
            if saved!=list(config['actor_hidden']) or any(a!=config['actor_activation'] for a in activations):
                raise ValueError('Architecture change requires explicit fresh initialization; actor transfer cannot reshape weights')
        for key in ('observation_size','floor_friction_version','termination_mode','action_schema_version'):
            expected={'observation_size':OBSERVATION_SIZE,'floor_friction_version':env.environment_contract['floor_friction_version'],'termination_mode':'balance_v2','action_schema_version':ACTION_SCHEMA_VERSION}[key]
            if data.get(key)!=expected: raise ValueError(f'Source requires a different physical/interface contract: {key}')
    else:
        actor=AuraPolicy(env.observation_size,hidden_sizes=tuple(config['actor_hidden']),activation=config['actor_activation'],initialization=config['initialization'])
    actor.environment_contract=env.environment_contract
    actor.training_algorithm = config['algorithm']
    return actor.to(device)


def _evaluation(env,actor,seeds,service,critic=None):
    index=env.batch_index
    try:
        deterministic=env.collect_batch(actor,1,gradients=False,stochastic=False,seed=0,service=service)['balance_metrics'][0]
        stochastic_data=env.collect_batch(actor,len(seeds),gradients=False,evaluation_seeds=seeds,service=service)
        stochastic=stochastic_data['balance_metrics']
    finally:env.batch_index=index
    result = dict(deterministic=deterministic,stochastic=stochastic,
        mean={key:mean(row[key] for row in stochastic) for key in METRICS},
        standard_deviation={key:stdev(row[key] for row in stochastic) if len(stochastic)>1 else 0. for key in METRICS})
    if critic is not None:
        from .ppo_experiment import critic_metrics
        result['critic_accuracy']=critic_metrics(critic,stochastic_data)
    return result


def _run_experiment(path, *, progress=None, service=None, diagnostic_limit=None,stage=None):
    path=Path(path);original=json.loads((path/'configuration.json').read_text())
    frozen=path/'source_snapshot.pt'
    if frozen.exists():
        expected=json.loads((path/'source.json').read_text())['sha256']
        if hashlib.sha256(frozen.read_bytes()).hexdigest()!=expected:
            raise ValueError('Frozen source snapshot hash does not match the reviewed experiment')
    execution=dict(original,source=str(frozen)) if frozen.exists() else dict(original)
    if original['comparison']=='source':
        snapshots=json.loads((path/'compared_sources.json').read_text())
        if [item['path'] for item in snapshots]!=original['values']:
            raise ValueError('Compared actor snapshots do not match the reviewed configuration')
        execution['values']=[]
        for item in snapshots:
            snapshot=path/item['snapshot']
            if hashlib.sha256(snapshot.read_bytes()).hexdigest()!=item['sha256']:
                raise ValueError('Compared actor snapshot hash does not match the reviewed experiment')
            execution['values'].append(str(snapshot))
    config=validate(execution)
    state=json.loads((path/'state.json').read_text())
    if state['status']!='draft': raise ValueError('Completed or interrupted experiments are immutable; duplicate configuration for a new run')
    torch.set_num_threads(1)
    source_hash=hashlib.sha256(Path(config['source']).read_bytes()).hexdigest() if config['source'] else None
    import aura_python
    provenance=dict(source_checkpoint=original['source'],source_sha256=source_hash,
        simulator_sha256=hashlib.sha256(Path(aura_python.__file__).read_bytes()).hexdigest(),
        source_files={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
            (ROOT/'python').rglob('*.py') if '__pycache__' not in p.parts},
        torch_version=str(torch.__version__),hardware=capabilities()['hardware'],diagnostic_limit=diagnostic_limit)
    atomic_json(path/'provenance.json',provenance)
    atomic_json(path/'state.json',dict(status='running',started_utc=datetime.now(timezone.utc).isoformat()))
    results=[];started=time.monotonic()
    try:
        for label,branch in branches(config):
            for seed in branch['training_seeds']:
                if stage:stage(f'{label} / seed {seed} / preparing workers')
                if service: service()
                env=IndependentEnv(branch['workers'],diagnostic_limit=diagnostic_limit);env.seed=seed
                settings=reward_settings(3,branch['reward_formula'],branch['reward_parameters'])
                apply_reward(env,settings)
                actor=_source(branch,env,seed,resolve_training_device(branch['device']))
                config_train=TrainingConfig(batch_size=branch['episodes'],agents=branch['workers'],seed=seed,
                    learning_rate=branch['actor_lr'],device=branch['device'],actor_design=actor.design,
                    critic_design=NetworkDesign(branch['critic_hidden'],branch['critic_activation'],branch['initialization']))
                if service: service.environment=env
                if stage:stage(f'{label} / seed {seed} / evaluating starting policy')
                initial=_evaluation(env,actor,branch['evaluation_seeds'],service)
                row=dict(branch=label,seed=seed,initial=initial,updates=[],configuration=branch,
                    environment_contract=env.environment_contract,worker_warning=worker_warning(branch['workers']))
                if config['comparison']=='source':
                    source_index=0 if label=='control' else int(label.split('_')[-1])
                    row['source_checkpoint']=([original['source']]+original['values'])[source_index]
                    row['source_sha256']=hashlib.sha256(Path(branch['source']).read_bytes()).hexdigest()
                if branch['kind'] in ('training','critic_fit'):
                    optimizer=torch.optim.Adam(actor.parameters(),lr=branch['actor_lr'])
                    critic,critic_optimizer=create_critic(env,config_train)
                    for group in critic_optimizer.param_groups: group['lr']=branch['critic_lr']
                    for update in range(branch['updates']):
                        if stage:stage(f'{label} / seed {seed} / collecting and updating {update+1}/{branch["updates"]}')
                        if service: service()
                        env.batch_index=update
                        if branch['kind']=='critic_fit':
                            data=env.collect_batch(actor,branch['episodes'],gradients=False,service=service)
                            for fit in range(branch['critic_fits']):
                                if service:service()
                                critic_loss=train_critic(critic,critic_optimizer,data['states_by_episode'],data['returns_by_episode'])
                            result=dict(critic_loss=critic_loss,episode_scores=data['episode_scores'],collection={'balance_metrics':data['balance_metrics']},max_parameter_change=0.)
                        elif branch['algorithm']=='ppo_clip':
                            from .ppo_experiment import PPOConfig,update_ppo
                            data=env.collect_batch(actor,branch['episodes'],gradients=False,record_policy=True,service=service)
                            result=update_ppo(actor,optimizer,critic,critic_optimizer,data,PPOConfig(critic_steps=branch['critic_fits']))
                        else:
                            env.runtime_service=service
                            # The same production update, with the collector servicing cancellation.
                            collect_original=env.collect_batch
                            env.collect_batch=lambda *args,**kw: collect_original(*args,service=service,**kw)
                            try: result=train_batch(env,actor,optimizer,branch['episodes'],critic=critic,critic_optimizer=critic_optimizer)
                            finally:env.collect_batch=collect_original
                            for fit in range(branch['critic_fits']-1):
                                if service:service()
                                train_critic(critic,critic_optimizer,result['states_by_episode'],result['returns_by_episode'])
                        if stage:stage(f'{label} / seed {seed} / evaluating update {update+1}/{branch["updates"]}')
                        evaluated=_evaluation(env,actor,branch['evaluation_seeds'],service,critic)
                        record=dict(update=update+1,evaluation=evaluated,
                            actor_loss=result.get('loss',result.get('actor_loss')),
                            critic_loss=result.get('critic_loss'),worker_count=branch['workers'],
                            deterministic=evaluated['deterministic'],stochastic=evaluated['mean'],
                            episodes_completed=(update+1)*branch['episodes'],
                            mean_survival=mean(result.get('episode_scores',[0])),
                            gradient_norm=math.sqrt(sum(p.grad.square().sum().item() for p in actor.parameters() if p.grad is not None)),
                            max_parameter_change=result.get('max_parameter_change',0.),
                            episodes=[dict(index=update*branch['episodes']+i+1,**m) for i,m in enumerate(
                                (result.get('collection') or {}).get('balance_metrics',data['balance_metrics'] if branch['algorithm']=='ppo_clip' else []))])
                        row['updates'].append(record)
                        output=path/f'{label}-seed{seed}.pt'
                        save_checkpoint(actor,optimizer,branch['episodes']*(update+1),output,
                            critic=critic,critic_optimizer=critic_optimizer,training_seed=seed,
                            training_history=row['updates'],training_schedule=dict(agents=branch['workers'],episodes_per_batch=branch['episodes']))
                        row['checkpoint']=str(output)
                        if progress: progress(f'{label} / seed {seed} / update {update+1}/{branch["updates"]}',evaluated['mean'])
                elif branch['kind']=='benchmark':
                    if stage:stage(f'{label} / seed {seed} / measuring collection throughput')
                    data=env.collect_batch(actor,branch['episodes'],gradients=False,seed=seed,service=service)
                    row.update(episode_seeds=data['episode_seeds'],worker_pids=data['worker_pids'],collection_seconds=data['collection_seconds'],
                        episodes_per_second=branch['episodes']/data['collection_seconds'],physics_steps_per_second=sum(data['episode_scores'])/data['collection_seconds'])
                elif branch['kind']=='reward_audit':
                    if stage:stage(f'{label} / seed {seed} / auditing reward components')
                    data=env.collect_batch(actor,branch['episodes'],gradients=False,seed=seed,service=service)
                    row['per_episode_components']=data['reward_components']
                    total_steps=sum(data['episode_scores'])
                    row['mean_components']={key:sum(component[key]*steps for component,steps in zip(data['reward_components'],data['episode_scores']))/total_steps
                        for key in data['reward_components'][0]}
                    row['sampling']='every realized physical transition; terminal reward remains separately zero'
                    row['episode_seeds']=data['episode_seeds']
                row['final']=row['updates'][-1]['evaluation'] if row['updates'] else initial
                results.append(row)
                atomic_json(path/'results.json',dict(schema_version=1,rows=results,elapsed_seconds=time.monotonic()-started))
                if progress: progress(f'{label} / seed {seed} / complete',row['final']['mean'])
        atomic_json(path/'comparison.json',comparison_results(results))
        if source_hash and hashlib.sha256(Path(config['source']).read_bytes()).hexdigest()!=source_hash:
            raise RuntimeError('Source checkpoint changed externally while the experiment ran')
        atomic_json(path/'state.json',dict(status='completed',elapsed_seconds=time.monotonic()-started,
            validation='not promoted; criteria require explicit review',
            source_snapshot_unchanged=True,source_unchanged=(not original['source'] or (Path(original['source']).exists() and hashlib.sha256(Path(original['source']).read_bytes()).hexdigest()==source_hash))))
    except BaseException as error:
        atomic_json(path/'state.json',dict(status='cancelled' if isinstance(error,KeyboardInterrupt) else 'failed',
            error=str(error),elapsed_seconds=time.monotonic()-started))
        (path/'error.txt').write_text(traceback.format_exc())
        raise
    return results


def comparison_results(rows):
    """Paired final differences against the control under the same training seed."""
    controls={row['seed']:row for row in rows if row['branch']=='control'}
    paired=[]
    for row in rows:
        control=controls.get(row['seed'])
        if row['branch']=='control' or control is None:continue
        paired.append(dict(branch=row['branch'],training_seed=row['seed'],
            evaluation_seeds=row['configuration']['evaluation_seeds'],
            final_difference={key:row['final']['mean'][key]-control['final']['mean'][key] for key in METRICS},
            improvement_difference={key:(row['final']['mean'][key]-row['initial']['mean'][key])-
                (control['final']['mean'][key]-control['initial']['mean'][key]) for key in METRICS}))
    return dict(schema_version=1,paired=paired,interpretation='variant minus matched control; positive fall rate is worse',
        limitations=['Neutral reset only; no unseen-posture claim','Small seed sets do not establish statistical significance',
            'Different episode budgets are explicit; equal updates do not imply equal physics cost',
            'Recovery count is available; recovery time is not yet measured'])


def promote(path,branch='control',directory=TEMPLATE_DIR):
    path=Path(path);config=validate(json.loads((path/'configuration.json').read_text()))
    if json.loads((path/'state.json').read_text())['status']!='completed': raise ValueError('Only completed experiments can be reviewed for promotion')
    provenance=json.loads((path/'provenance.json').read_text())
    if provenance.get('diagnostic_limit') is not None or config['kind'] in ('benchmark','reward_audit','critic_fit'):
        raise ValueError('Diagnostic budgets and non-evaluation studies cannot validate a standing template')
    rows=[row for row in json.loads((path/'results.json').read_text())['rows'] if row['branch']==branch]
    criteria=config['criteria']
    if len(rows)<criteria['minimum_seeds'] or any(row['final']['mean']['balanced_seconds']<criteria['min_balanced_seconds'] or row['final']['mean']['fall_rate']>criteria['max_fall_rate'] for row in rows):
        raise ValueError('Candidate does not meet its predeclared physical criteria across enough training seeds')
    c=rows[0]['configuration']
    if c['reward_formula']!='product' or c['reward_parameters']:
        raise ValueError('Reward candidates remain experimental; validate this architecture under the production objective before making it the default')
    if config['kind']=='baseline' and config['source'] and criteria['minimum_seeds']>1:
        raise ValueError('One frozen source actor cannot count as multiple independently initialized seeds')
    data=template(config['name']+'-'+branch,
        NetworkDesign(c['actor_hidden'],c['actor_activation'],c['initialization']),NetworkDesign(c['critic_hidden'],c['critic_activation'],c['initialization']))
    saved=save_template(data,directory)
    atomic_json(Path(directory)/'default.json',dict(schema_version=1,template=str(saved),experiment=str(path),branch=branch,
        criteria=criteria,meaning='architecture with fresh weights; not a trained checkpoint',validated_task='neutral standing under recorded evaluation seeds'))
    atomic_json(path/'promotion.json',dict(template=str(saved),branch=branch,criteria=criteria))
    return saved


def run_experiment(path, **options):
    import fcntl
    from contextlib import ExitStack
    folders=[Path(path)]
    while folders[-1].parent.name in ('runs','audits'):
        folders.append(folders[-1].parent.parent)
    # Hold parent locks too: deleting a workspace must not move an active child run.
    with ExitStack() as locks:
        for folder in reversed(folders):
            lock=locks.enter_context((folder/'.run.lock').open('a'))
            try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError as exc:raise ValueError('Experiment or linked workspace is already running') from exc
        return _run_experiment(path,**options)
