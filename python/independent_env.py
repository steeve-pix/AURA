"""Spawned, isolated physics episodes with one frozen CPU actor per batch.

Episode seeds depend on batch seed and logical episode index, never completion
order. Tensor payloads travel as bytes so worker lifetime does not own tensor FDs.
Only the parent reconstructs differentiable action probabilities and optimizes.
"""
import io
import multiprocessing as mp
import os
import queue
import time
from concurrent.futures import ProcessPoolExecutor, wait, FIRST_COMPLETED
from copy import deepcopy
from statistics import mean
import torch
from .aura_env import AuraEnv
from .aura_policy import outputs_to_action
from .worker_config import validate_workers,default_workers


BATCH_SEED_STRIDE = 1000000007
EPISODE_SEED_STRIDE = 1000003
SEED_MODULUS = 2**63 - 1

def batch_seed(seed, update):
    return (int(seed) + BATCH_SEED_STRIDE * int(update)) % SEED_MODULUS

def episode_seed(seed, episode):
    return (int(seed) + EPISODE_SEED_STRIDE * int(episode)) % SEED_MODULUS


def _episode(actor, quality_version, seed, stochastic, episode, worker, events, limit, paused=None, reward_settings=None):
    torch.set_num_threads(1)
    env = AuraEnv(quality_version=quality_version)
    from .experiment_rewards import apply_reward
    apply_reward(env,reward_settings)
    generator = torch.Generator().manual_seed(seed)
    states, samples, means, rewards = [], [], [], []
    component_sums = {}
    state, info = env.reset()
    started = time.monotonic()
    with torch.inference_mode():
        while not info['done']:
            while paused is not None and paused.is_set(): time.sleep(.02)
            states.append(state.clone())
            mu = actor(state)
            sample = mu + torch.randn(mu.shape, generator=generator) * actor.exploration_std if stochastic else mu
            samples.append(sample.clone()); means.append(mu.clone())
            action = outputs_to_action(sample.clamp(*actor.sample_bounds), actor.action_transforms)
            state, reward, _, _, info = env.step(action)
            rewards.append(reward)
            for key,value in info["reward_qualities"].items(): component_sums[key]=component_sums.get(key,0.)+value
            if limit is not None and len(rewards) >= limit and not info['done']:
                info.update(done=True, truncated=True, termination_reason='diagnostic_budget')
                info['balance_metrics']['termination_reason'] = 'diagnostic_budget'
            info.update(worker_id=worker, episode_seed=seed, arena_contacts=0)
            info['balance_metrics'].update(worker_id=worker, episode_seed=seed)
            if events is not None and (len(rewards) == 1 or len(rewards) % 24 == 0 or info['done']):
                events.put((episode, worker, info, action, reward, sum(rewards)))
    result = dict(component_means={key:value/len(rewards) for key,value in component_sums.items()}, states=torch.stack(states), samples=torch.stack(samples), means=torch.stack(means),
                  rewards=rewards, info=info, action=action, worker=worker, seed=seed,
                  pid=os.getpid(), duration=time.monotonic()-started)
    buffer = io.BytesIO(); torch.save(result, buffer)
    return buffer.getvalue()


def _wave(actor, quality_version, jobs, stochastic, worker, events, limit, paused=None, reward_settings=None):
    return [(index, _episode(actor, quality_version, seed, stochastic, index, worker, events, limit, paused, reward_settings))
            for index, seed in jobs]


class IndependentEnv(AuraEnv):
    is_arena = True  # existing collector protocol, not a coupled physical arena
    is_independent = True
    spacing = 0.

    def __init__(self, workers=None, *, quality_version=None, diagnostic_limit=None):
        super().__init__(quality_version=quality_version)
        self.agents = validate_workers(default_workers() if workers is None else workers)
        self.diagnostic_limit = diagnostic_limit
        self.snapshots = {}
        self.selected_worker = 0
        self.batch_index = 0
        self.seed = 42

    @property
    def environment_contract(self):
        return {**super().environment_contract, 'environment_version': 6,
                'collection_mode': 'independent', 'collection_seed_version': 1,
                'experimental_reward': getattr(self,'experimental_reward',None)}

    def collect_batch(self, policy, episodes, *, gradients=True, stochastic=True, seed=None,
                      service=None, progress=None, finished=None, record_policy=False, evaluation_seeds=None):
        from .train_nn import discounted_returns
        if type(episodes) is not int or episodes < 1:
            raise ValueError('Positive episode budget required')
        collection_seed = batch_seed(self.seed,self.batch_index) if seed is None else seed
        actor = deepcopy(policy).cpu().eval()
        for parameter in actor.parameters(): parameter.requires_grad_(False)
        context = mp.get_context('spawn')
        count = min(self.agents, episodes)
        rows = {}
        started = time.monotonic()
        # A manager queue carries sparse UI snapshots, never a physics object.
        manager = context.Manager() if progress or service else None
        events = manager.Queue() if manager else None
        self.pause_event = manager.Event() if manager else None
        completed = set()
        pool = ProcessPoolExecutor(max_workers=count, mp_context=context)
        futures = []
        def dispatch():
            if events is None: return
            while True:
                try: event = events.get_nowait()
                except queue.Empty: break
                index, worker, info, action, reward, total = event
                self.snapshots[worker] = info
                if progress: progress(index, worker, info, action, reward, total)
                if info["done"] and index not in completed:
                    completed.add(index)
                    if finished: finished(index,info,action,reward,total)
        try:
            for worker in range(count):
                jobs = [(index, evaluation_seeds[index] if evaluation_seeds is not None else episode_seed(collection_seed, index)) for index in range(worker, episodes, count)]
                futures.append(pool.submit(_wave, actor, self.quality_version, jobs, stochastic, worker,
                                           events, self.diagnostic_limit, self.pause_event, getattr(self,"experimental_reward",None)))
            pending = set(futures)
            while pending:
                if service: service()
                dispatch()
                done, pending = wait(pending, timeout=.05, return_when=FIRST_COMPLETED)
                for future in done:
                    for index, payload in future.result():
                        rows[index] = torch.load(io.BytesIO(payload), weights_only=True)
            dispatch()
        except BaseException:
            for future in futures: future.cancel()
            # Python 3.14's public termination API prevents cancellation waiting
            # for a long wave. Older runtimes require explicit child termination.
            if hasattr(pool, 'terminate_workers'): pool.terminate_workers()
            else:
                for process in getattr(pool, '_processes', {}).values(): process.terminate()
            raise
        finally:
            pool.shutdown(wait=True, cancel_futures=True)
            if manager: manager.shutdown()
            self.pause_event = None
        if sorted(rows) != list(range(episodes)):
            raise RuntimeError('Incomplete worker batch; no optimizer update is permitted')
        data = {key: [] for key in ('states_by_episode','samples_by_episode','means_by_episode',
                'log_probs_by_episode','returns_by_episode','episode_scores','episode_returns','balance_metrics')}
        data.update(reward_components=[],contact_pairs=0, episode_seeds=[], worker_ids=[], worker_pids=[], collection_seconds=time.monotonic()-started)
        for index in range(episodes):
            row = rows[index]; states = row['states']; samples = row['samples']
            with torch.enable_grad() if gradients else torch.no_grad():
                mu = policy(states)
                logs = torch.distributions.Normal(mu, policy.exploration_std).log_prob(samples.to(mu)).sum(-1)
            data['states_by_episode'].append(list(states.unbind()))
            data['samples_by_episode'].append(list(samples.unbind()))
            data['means_by_episode'].append(list(row['means'].unbind()))
            data['log_probs_by_episode'].append(list(logs.unbind()))
            data['returns_by_episode'].append(discounted_returns(row['rewards']))
            data['episode_scores'].append(len(row['rewards']))
            data['episode_returns'].append(sum(row['rewards']))
            metrics = row['info']['balance_metrics']; metrics.update(worker_id=row['worker'], episode_seed=row['seed'])
            data['balance_metrics'].append(metrics)
            data['reward_components'].append(row['component_means'])
            data['episode_seeds'].append(row['seed']); data['worker_ids'].append(row['worker']); data['worker_pids'].append(row['pid'])
            self.info = row['info']; self.snapshots[row['worker']] = row['info']
            if finished and index not in completed: finished(index, row['info'], row['action'], row['rewards'][-1], sum(row['rewards']))
        self.batch_index += 1
        return data

    def evaluate_arena(self, policy, *, stochastic=False, seed=10042, service=None):
        index = self.batch_index
        try:
            data = self.collect_batch(policy, 3 if stochastic else 1, gradients=False,
                                      stochastic=stochastic, seed=seed, service=service)
        finally:
            self.batch_index = index
        rows = data['balance_metrics']
        result = {key: mean(row[key] for row in rows) for key in rows[0]
                  if isinstance(rows[0][key], (int,float)) and key not in ('worker_id','episode_seed')}
        for key in ('steps','balanced_steps','unbalanced_steps','longest_balanced_steps','recoveries'):
            if key in result: result[key] = round(result[key])
        reasons = {row['termination_reason'] for row in rows}
        result.update(termination_reason=next(iter(reasons)) if len(reasons)==1 else 'mixed',
                      stochastic=stochastic, exploration_seed=seed if stochastic else None, agent_metrics=rows)
        return result

    def set_paused(self, value):
        event = getattr(self, 'pause_event', None)
        if event is not None:
            event.set() if value else event.clear()
