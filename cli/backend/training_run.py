"""Select a checkpoint and own its write lock for one training session."""

import fcntl
import hashlib
import io
import re
import torch
from dataclasses import replace
from pathlib import Path
from uuid import uuid4

from python.aura_env import AuraEnv
from python.model_checkpoint import MODEL_DIR, load_checkpoint, save_checkpoint
from python.train_nn import create_training, create_critic, resolve_training_device


class TrainingRun:
    def __init__(self, config, mode="new", name="aura_balance", checkpoint=None, directory=MODEL_DIR):
        self.config = config
        self.mode = mode
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.lock = None
        self.episodes_completed = 0
        self.history = []
        self.initial_actor = None
        self.updates_completed = 0
        self.run_id = uuid4().hex
        try:
            if mode in ("new", "fork"):
                if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", name):
                    raise ValueError("Model names need 1–64 letters, numbers, underscores or hyphens")
                base = name.lower()
                if base.endswith("_best"):
                    raise ValueError("The _best suffix is reserved for read-only evaluation checkpoints")
                suffix = 1
                while True:
                    filename = base + (f"_{suffix}" if suffix > 1 else "") + ".pt"
                    self.path = self.directory / filename
                    existing = {path.name.casefold() for path in self.directory.glob("*.pt")}
                    if filename.casefold() not in existing and self.acquire():
                        if not self.path.exists():
                            break
                        self.release()
                    suffix += 1
            elif mode in ("resume", "restart"):
                if checkpoint is None:
                    raise ValueError("Choose a model first")
                self.path = Path(checkpoint).resolve()
                if self.path.stem.casefold().endswith("_best"):
                    raise ValueError("Best checkpoint is read-only; train the latest model instead")
                if self.path.parent != self.directory or self.path.suffix != ".pt" or not self.path.is_file():
                    raise ValueError("Choose an existing checkpoint in models/")
                if not self.acquire():
                    raise ValueError("This model is already being trained in another session")
            else:
                raise ValueError("Choose resume, new, fork or restart")

            from python.independent_env import IndependentEnv
            def resume_environment(saved):
                if config.collection_mode == 'historical':
                    if config.agents == 1: return AuraEnv()
                    from python.arena_env import make_environment
                    return make_environment(config.agents,spacing=saved.get('arena_spacing',12.))
                return IndependentEnv(config.agents)
            if mode == "restart":
                from python.model_checkpoint import validate_checkpoint_contract
                restart_data = torch.load(self.path,map_location="cpu",weights_only=True)
                restart_env = resume_environment(restart_data)
                validate_checkpoint_contract(restart_data, restart_env)
                from python.model_checkpoint import policy_from_checkpoint
                from python.model_templates import NetworkDesign
                config = replace(config, actor_design=policy_from_checkpoint(restart_data,restart_env).design,
                    critic_design=NetworkDesign(**restart_data['critic']['design']) if 'critic' in restart_data else config.critic_design)
                self.config = config
            if mode == "resume":
                saved = torch.load(self.path, map_location="cpu", weights_only=True)
                self.env = resume_environment(saved)
                if config.collection_mode!='historical' and saved.get('reward_version',1)!=3:
                    raise ValueError('Checkpoint uses a different reward/termination objective; historical replay only')
                if config.collection_mode!='historical' and saved.get('collection_mode') != 'independent':
                    raise ValueError('Historical solo/shared-arena collector: use Fork actor to migrate explicitly to independent workers with a fresh critic and optimizer')
                self.policy, data = load_checkpoint(self.path, self.env)
                if data.get("reward_version",1)==3 and data.get("training_algorithm") != "actor_critic_mc":
                    raise ValueError("Experimental algorithm: duplicate its experiment to continue, or Fork actor for production MC training")
                self.history = list(data.get("training_history", []))
                self.initial_actor = data.get("initial_actor")
                if "fork_source" in data:
                    self.policy.fork_source = data["fork_source"]
                self.policy.to(resolve_training_device(config.device))
                self.run_id = data.get("training_run_id")
                self.optimizer = torch.optim.Adam(self.policy.parameters())
                self.optimizer.load_state_dict(data["optimizer"])
                from python.model_templates import NetworkDesign
                critic_design = NetworkDesign(**data['critic'].get('design', {'hidden_sizes': data['critic']['architecture'][1:-1]})) if 'critic' in data else config.critic_design
                self.config = replace(config, critic_design=critic_design)
                self.critic, self.critic_optimizer = create_critic(self.env, self.config)
                if getattr(self.env, 'reward_version', 1) == 3:
                    self.critic.load_state_dict(data['critic']['model'], strict=True)
                    self.critic_optimizer.load_state_dict(data['critic']['optimizer'])
                    if not all(torch.isfinite(p).all() for p in self.critic.parameters()):
                        raise ValueError("Saved critic contains non-finite parameters")
                self.episodes_completed = data["episodes_completed"]
                self.updates_completed = data.get("updates_completed", max(
                    (int(state.get("step", 0)) for state in self.optimizer.state.values()), default=0))
                self.config = replace(self.config, seed=data.get("training_seed", config.seed))
                if getattr(self.env,"is_independent",False):
                    self.env.seed = self.config.seed
                    self.env.batch_index = self.updates_completed
                previous_workers = data.get("training_schedule", {}).get("agents", 1)
                self.worker_changes = list(data.get("worker_changes", []))
                if previous_workers != config.agents:
                    self.worker_changes.append(dict(update=self.updates_completed, before=previous_workers, after=config.agents))
                if "rng_state" in data:
                    torch.set_rng_state(data["rng_state"])
                else:
                    torch.manual_seed(self.config.seed)
                saved_device_rng = data.get("device_rng_states", {})
                device = resolve_training_device(config.device)
                if device.type == "cuda" and "cuda" in saved_device_rng:
                    torch.cuda.set_rng_state_all(saved_device_rng["cuda"])
                elif device.type == "mps" and "mps" in saved_device_rng:
                    torch.mps.set_rng_state(saved_device_rng["mps"])
            else:
                self.env, self.policy, self.optimizer = (create_training(config, environment=restart_env)
                                                        if mode == "restart" and (config.agents>1 or config.collection_mode=="independent")
                                                        else create_training(config))
                self.critic, self.critic_optimizer = create_critic(self.env, config)
            if mode == "fork":
                from python.arena_env import checkpoint_environment, make_environment
                from python.model_checkpoint import policy_from_checkpoint
                source = Path(checkpoint).resolve() if checkpoint else None
                if source is None or not source.is_file():
                    raise ValueError("Choose a saved actor to fork")
                payload = source.read_bytes()
                data = torch.load(io.BytesIO(payload), map_location="cpu", weights_only=True)
                historical_env = checkpoint_environment(data)
                actor = policy_from_checkpoint(data, historical_env)
                if config.collection_mode == 'historical':
                    if config.agents != data.get('arena_agents',1): raise ValueError('Historical fork must retain arena agent count')
                    self.env = make_environment(config.agents,spacing=data.get('arena_spacing',12.))
                else:
                    self.env = IndependentEnv(config.agents)
                    self.env.seed = config.seed
                migration_keys = {'quality_version','quality_config','environment_version','collection_mode',
                    'collection_seed_version','experimental_reward','arena_agents','arena_spacing','arena_collisions','arena_collision_version',
                    'arena_finished_agents','evaluation_summary'}
                differences = {k for k,value in self.env.environment_contract.items() if data.get(k)!=value}
                if differences - migration_keys:
                    raise ValueError('Fork requires matching physical, observation, action and termination contracts')
                self.policy = actor.to(resolve_training_device(config.device))
                self.policy.environment_contract = self.env.environment_contract
                self.policy.fork_source = dict(filename=source.name, sha256=hashlib.sha256(payload).hexdigest(),
                    quality_version=data.get('quality_version'), updates=data.get('updates_completed', 0),
                    optimizer_and_critic='fresh',
                    collector_migration=dict(source=data.get('collection_mode','shared_arena' if data.get('arena_agents',1)>1 else 'solo'),
                        target='independent' if getattr(self.env,'is_independent',False) else 'historical',
                        source_agents=data.get('arena_agents',1),target_workers=config.agents))
                self.optimizer = torch.optim.Adam(self.policy.parameters(), lr=config.learning_rate)
                self.critic, self.critic_optimizer = create_critic(self.env, config)
                torch.manual_seed(config.seed)
            if mode != "resume":
                self.initial_actor = {k: v.detach().cpu().clone() for k, v in self.policy.state_dict().items()}
        except BaseException:
            self.release()
            raise

    def acquire(self):
        handle = (self.directory / f".{self.path.name}.lock").open("a")
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            handle.close()
            return False
        self.lock = handle
        return True

    def release(self):
        if self.lock is not None:
            self.lock.close()
            self.lock = None

    def save(self, episode_count):
        episodes = self.episodes_completed + episode_count
        self.episodes_completed = episodes
        path = self.save_current()
        self.updates_completed += 1
        return path

    def save_current(self):
        """Persist the current state without counting another optimizer update."""
        return save_checkpoint(
            self.policy, self.optimizer, self.episodes_completed, self.path,
            training_seed=self.config.seed, training_run_id=self.run_id,
            critic=self.critic, critic_optimizer=self.critic_optimizer,
            training_history=self.history, initial_actor=self.initial_actor,
            worker_changes=getattr(self, "worker_changes", []),
            training_schedule=dict(episodes_per_batch=self.config.batch_size,
                                   consecutive_batches=self.config.batches, agents=self.config.agents),
        )

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.release()
