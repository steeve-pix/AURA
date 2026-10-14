"""Step 114: offline residual value learning on verified historical datasets.

No environment is constructed and no trajectories are collected. This evaluates
archived pre-elbow-correction physics, not today's corrected skeleton. Production
actor training and its leave-one-out baseline remain untouched.
"""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path

import torch

from .aura_env import OBSERVATION_SCHEMA_VERSION
from .aura_policy import AuraPolicy, AuraValue, ACTION_SCHEMA_VERSION
from .critic_error_analysis import BINS
from .timestep_baseline_analysis import fit_timestep_baseline
from .train_nn import CRITIC_RETURN_SCALE, DISCOUNT_FACTOR, prepare_critic_data


def residual_dataset(critic, dataset, table, constant):
    """Keep pre-action states aligned with scaled returns minus the fixed lookup."""
    if not table or not math.isfinite(constant) or any(not math.isfinite(v) for v in table):
        raise ValueError('Finite nonempty timestep baseline required')
    states, targets = prepare_critic_data(critic, dataset['states_by_episode'], dataset['returns_by_episode'])
    lengths = [len(episode) for episode in dataset['states_by_episode']]
    base = torch.tensor([table[t] if t < len(table) else constant for length in lengths for t in range(length)],
                        dtype=targets.dtype, device=targets.device).reshape(-1,1)
    if states.shape[1] != critic.network[0].in_features:
        raise ValueError('Observation size does not match critic')
    return states, targets, base, (targets-base).detach(), lengths


def evaluate_residual(critic, prepared, coverage_length=None):
    states, targets, base, residual_targets, lengths = prepared
    with torch.no_grad():
        corrections = critic(states)
        if corrections.shape != residual_targets.shape or not torch.isfinite(corrections).all():
            raise ValueError('Critic must produce finite [N, 1] residual predictions')
        # Double-precision arithmetic here keeps the comparison and bin aggregation
        # independent of MSE reduction rounding. Critic computation stays float32.
        error = base.double()+corrections.double()-targets.double()
        base_error = base.double()-targets.double()
    rows, offset = [], 0
    for length in lengths:
        for t in range(length): rows.append((t,error[offset+t,0].item(),base_error[offset+t,0].item()))
        offset += length
    def mse(values, field):
        return math.fsum(row[field]**2 for row in values)/len(values) if values else None
    result = {'combined_mse': mse(rows,1), 'timestep_mse': mse(rows,2),
              'bins': [{'range':f'{start}–{end-1}' if end else f'{start}+',
                        'count':len(values), 'combined_mse':mse(values,1), 'timestep_mse':mse(values,2)}
                       for start,end in BINS
                       for values in [[row for row in rows if row[0]>=start and (end is None or row[0]<end)]]]}
    if coverage_length is not None:
        if type(coverage_length) is not int or coverage_length<1:
            raise ValueError('Positive timestep coverage required')
        covered = [row for row in rows if row[0]<coverage_length]
        uncovered = [row for row in rows if row[0]>=coverage_length]
        result['coverage'] = {'covered_count':len(covered), 'uncovered_count':len(uncovered),
                              'covered_combined_mse':mse(covered,1), 'covered_timestep_mse':mse(covered,2),
                              'uncovered_combined_mse':mse(uncovered,1), 'uncovered_timestep_mse':mse(uncovered,2)}
    return result



def train_residual_critic(critic, training, validation, *, updates=20, rate=.001, measurement=None):
    """Fit only a fresh supplied critic; validation is evaluated, never optimized."""
    if type(updates) is not int or updates<1 or not 0<rate<float('inf'):
        raise ValueError('Positive update budget and finite learning rate required')
    _, train_targets = prepare_critic_data(critic,training['states_by_episode'],training['returns_by_episode'])
    lengths = [len(episode) for episode in training['states_by_episode']]
    scaled_training = [episode[:,0].tolist() for episode in train_targets.split(lengths)]
    constant, table, counts = fit_timestep_baseline(scaled_training)
    train = residual_dataset(critic,training,table,constant)
    valid = residual_dataset(critic,validation,table,constant)
    optimizer = torch.optim.Adam(critic.parameters(),lr=rate)
    measurements = []
    def measure(update):
        critic.eval()
        train_metrics = evaluate_residual(critic,train)
        validation_metrics = evaluate_residual(critic,valid)
        row = {'update':update, 'training_residual_mse':train_metrics['combined_mse'],
               'validation_combined_mse':validation_metrics['combined_mse'],
               'training_bins':train_metrics['bins'], 'validation_bins':validation_metrics['bins']}
        measurements.append(row)
        if measurement: measurement(row)
    measure(0)
    for update in range(1,updates+1):
        critic.train()
        optimizer.zero_grad(set_to_none=True)
        predictions = critic(train[0])
        if predictions.shape != train[3].shape:
            raise ValueError('Residual predictions and targets must have matching [N, 1] shape')
        loss = torch.nn.functional.mse_loss(predictions,train[3])
        if not torch.isfinite(loss): raise RuntimeError('Nonfinite residual loss')
        loss.backward()
        if any(p.grad is not None and not torch.isfinite(p.grad).all() for p in critic.parameters()):
            raise RuntimeError('Nonfinite residual gradients')
        optimizer.step()
        if update in (1,5,10,20) or update==updates: measure(update)
    critic.eval()
    fallback = sum(max(0,len(episode)-len(table)) for episode in validation['states_by_episode'])
    return {'constant_scaled_prediction':constant, 'timestep_scaled_predictions':table,
            'training_observations_per_timestep':counts, 'measurements':measurements,
            'training_timesteps':len(train[0]), 'validation_timesteps':len(valid[0]),
            'validation_fallback_count':fallback, 'validation_fallback_percent':100*fallback/len(valid[0]),
            'timestep_validation_mse':evaluate_residual(critic,valid)['timestep_mse']}, optimizer


def run_archived_experiment(training_path, validation_path, checkpoint_path, reference_path, timestep_report_path):
    paths=[Path(p) for p in (training_path,validation_path,checkpoint_path,reference_path,timestep_report_path)]
    contents=[path.read_bytes() for path in paths]
    hashes=[hashlib.sha256(value).hexdigest() for value in contents]
    training,validation,saved=[torch.load(io.BytesIO(value),map_location='cpu',weights_only=True) for value in contents[:3]]
    reference,step113=[json.loads(value) for value in contents[3:]]
    for actual,expected in zip(hashes[:3],[step113['training_dataset_sha256'],step113['validation_sha256'],step113['checkpoint_sha256']]):
        if actual!=expected: raise ValueError('Historical artifact hash does not match Step 113')
    if (reference['observation_schema']!=OBSERVATION_SCHEMA_VERSION or reference['action_schema']!=ACTION_SCHEMA_VERSION or
        reference['return_scale']!=CRITIC_RETURN_SCALE or reference['discount_factor']!=DISCOUNT_FACTOR or
        saved['kind']!='aura_value' or reference['actor_seed']!=42):
        raise ValueError('Unsupported historical schema, return units or actor')
    for name,data in [('training',training),('validation',validation)]:
        if (len(data['states_by_episode'])!=reference[f'{name}_episodes'] or
            data['episode_scores']!=reference[f'{name}_survival_steps'] or
            [len(ep) for ep in data['states_by_episode']]!=data['episode_scores']):
            raise ValueError(f'{name} episode split does not match historical data')
    with torch.random.fork_rng(devices=[]):
        torch.manual_seed(reference['actor_seed'])
        actor = AuraPolicy(reference['observation_size']).eval().requires_grad_(False)
        torch.manual_seed(reference['critic_seed'])
        critic = AuraValue(reference['observation_size'])
        original = AuraValue(reference['observation_size']).eval().requires_grad_(False)
    original.load_state_dict(saved['model'],strict=True)
    actor_before=[p.detach().clone() for p in actor.parameters()]
    critic_before=[p.detach().clone() for p in critic.parameters()]
    # Verify the old critic reproduces its recorded MSE on both archived splits.
    for name,data in [('training',training),('validation',validation)]:
        states,targets=prepare_critic_data(original,data['states_by_episode'],data['returns_by_episode'])
        if states.shape!=(reference[f'{name}_timesteps'],reference['observation_size']):
            raise ValueError('Historical tensor layout mismatch')
        with torch.no_grad(): mse=torch.nn.functional.mse_loss(original(states),targets).item()
        if not math.isclose(mse,reference['measurements'][-1][f'{name}_mse'],rel_tol=1e-6,abs_tol=1e-8):
            raise ValueError('Original critic predictions no longer match the archive')
    result,optimizer=train_residual_critic(critic,training,validation,measurement=lambda row:
        print(f"Update {row['update']}: training residual MSE {row['training_residual_mse']:.9f} / validation combined MSE {row['validation_combined_mse']:.9f}",flush=True))
    if not math.isclose(result['timestep_validation_mse'],step113['overall']['timestep_mse_with_fallback'],rel_tol=1e-6,abs_tol=1e-8):
        raise ValueError('Timestep benchmark changed')
    result['final_coverage_metrics'] = evaluate_residual(critic,
        residual_dataset(critic,validation,result['timestep_scaled_predictions'],result['constant_scaled_prediction']),
        len(result['timestep_scaled_predictions']))['coverage']
    actor_change=max((p-old).abs().max().item() for p,old in zip(actor.parameters(),actor_before))
    if actor_change or any(p.grad is not None for p in actor.parameters()): raise RuntimeError('Actor was modified')
    if any(path.read_bytes()!=value for path,value in zip(paths,contents)): raise RuntimeError('Historical inputs changed')
    result.update(actor_max_parameter_change=actor_change,actor_has_no_gradients=True,actor_optimizer_updates=0,
                  critic_max_parameter_change=max((p-old).abs().max().item() for p,old in zip(critic.parameters(),critic_before)),
                  critic_updates=20,learning_rate=.001,critic_seed=reference['critic_seed'],actor_seed=reference['actor_seed'],
                  return_scale=CRITIC_RETURN_SCALE,discount_factor=DISCOUNT_FACTOR,observation_size=reference['observation_size'],
                  action_count=reference['action_count'],observation_schema=OBSERVATION_SCHEMA_VERSION,action_schema=ACTION_SCHEMA_VERSION,
                  training_episodes=len(training['states_by_episode']),validation_episodes=len(validation['states_by_episode']),
                  constant_validation_mse=reference['constant_validation_mse'],original_critic_validation_mse=reference['measurements'][-1]['validation_mse'],
                  original_physics_extension_sha256=reference['physics_extension_sha256'],torch_version=str(torch.__version__),
                  sources=[{'path':str(path),'sha256':value} for path,value in zip(paths,hashes)],
                  environment_steps=0,production_actor_baseline_changed=False,
                  units='G/100 minus training-only b_t; combined prediction = b_t + residual critic',
                  fallback='Training mean scaled return when timestep exceeds training coverage',
                  limitations='Historical pre-elbow-correction data; no current-physics claim. Training residuals use the in-sample timestep mean. Few late surviving episodes support the lookup. Validation is not optimized. One seed and one fixed actor do not establish gradient benefit.')
    checkpoint={'kind':'aura_value_residual','format_version':1,'model':critic.state_dict(),'optimizer':optimizer.state_dict(),
                'return_scale':CRITIC_RETURN_SCALE,'discount_factor':DISCOUNT_FACTOR,'observation_size':reference['observation_size'],
                'observation_schema_version':OBSERVATION_SCHEMA_VERSION,'action_schema_version':ACTION_SCHEMA_VERSION,
                'baseline':{'constant':result['constant_scaled_prediction'],'table':result['timestep_scaled_predictions'],
                            'training_counts':result['training_observations_per_timestep']},'experiment':result}
    return result,checkpoint


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--training',type=Path,default=Path('models/critics/step113-training.pt'))
    parser.add_argument('--validation',type=Path,default=Path('models/critics/step112-validation.pt'))
    parser.add_argument('--original-critic',type=Path,default=Path('models/critics/critic-d8moahss/critic.pt'))
    parser.add_argument('--reference',type=Path,default=Path('docs/critic-step110.json'))
    parser.add_argument('--timestep-report',type=Path,default=Path('docs/critic-step113.json'))
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--checkpoint-output',type=Path,required=True)
    args=parser.parse_args()
    inputs={p.resolve() for p in (args.training,args.validation,args.original_critic,args.reference,args.timestep_report)}
    if args.output.resolve()==args.checkpoint_output.resolve() or any(p.exists() or p.resolve() in inputs for p in (args.output,args.checkpoint_output)):
        parser.error('Choose distinct unused output paths; historical artifacts are preserved')
    report,checkpoint=run_archived_experiment(args.training,args.validation,args.original_critic,args.reference,args.timestep_report)
    with args.checkpoint_output.open('xb') as output: torch.save(checkpoint,output)
    report['residual_checkpoint']=str(args.checkpoint_output)
    with args.output.open('x') as output: json.dump(report,output,indent=2,allow_nan=False); output.write('\n')


if __name__=='__main__': main()
