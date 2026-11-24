"""Fixed-data critic fitting diagnostic; no actor optimizer or active model writes."""
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import torch
from python.arena_env import checkpoint_environment, make_environment
from python.aura_policy import AuraValue
from python.model_checkpoint import policy_from_checkpoint
from python.ppo_experiment import critic_metrics
from python.train_nn import train_critic


def run(source, output):
    torch.set_num_threads(1)
    source=Path(source);payload=source.read_bytes();digest=hashlib.sha256(payload).hexdigest()
    data=torch.load(io.BytesIO(payload),map_location='cpu',weights_only=True)
    actor=policy_from_checkpoint(data,checkpoint_environment(data))
    before=deepcopy(actor.state_dict())
    env=make_environment(data.get('arena_agents',1),spacing=data.get('arena_spacing',12.))
    training=env.collect_batch(actor,32,gradients=False,seed=294201)
    validation=env.collect_batch(actor,8,gradients=False,seed=394201)
    with torch.random.fork_rng(devices=[]):
        torch.manual_seed(43);critic=AuraValue(env.observation_size)
    optimizer=torch.optim.Adam(critic.parameters(),lr=.001)
    result=dict(source=source.name,sha256=digest,quality_version=env.quality_version,
        critic_hidden_sizes=[64,64],training_seed=294201,validation_seed=394201,
        training_episodes=32,heldout_episodes=8,actor_updates=0,measurements=[])
    for step in range(101):
        if step:train_critic(critic,optimizer,training['states_by_episode'],training['returns_by_episode'])
        if step in (0,1,5,10,20,50,100):
            row=dict(optimizer_steps=step,training=critic_metrics(critic,training),heldout=critic_metrics(critic,validation))
            result['measurements'].append(row)
            print(f'critic fits {step}: heldout EV {row["heldout"]["explained_variance"]:.4f} / '
                  f'raw RMSE {row["heldout"]["rmse_raw"]:.3f}',flush=True)
    if any(not torch.equal(v,actor.state_dict()[k]) for k,v in before.items()):raise RuntimeError('Actor changed')
    if any(p.grad is not None for p in actor.parameters()):raise RuntimeError('Actor has gradients')
    if hashlib.sha256(source.read_bytes()).hexdigest()!=digest:raise RuntimeError('Source changed externally')
    result['actor_unchanged']=True
    with Path(output).open('x') as handle:json.dump(result,handle,indent=2,allow_nan=False);handle.write('\n')
    return result


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=Path('models/aura_balance_env5_best.pt'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();run(args.source,args.output)
