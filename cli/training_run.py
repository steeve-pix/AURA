"""Select a checkpoint and own its write lock for one training session."""

from dataclasses import replace
from pathlib import Path
import fcntl
import re
from uuid import uuid4

import torch

from python.model_checkpoint import MODEL_DIR, load_checkpoint, save_checkpoint
from python.aura_env import AuraEnv
from python.train_nn import create_training


class TrainingRun:
    def __init__(self, config, mode="new", name="aura_balance", checkpoint=None, directory=MODEL_DIR):
        self.config = config
        self.mode = mode
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.lock = None
        self.episodes_completed = 0
        self.updates_completed = 0
        self.run_id = uuid4().hex
        try:
            if mode == "new":
                if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", name):
                    raise ValueError("Model names need 1–64 letters, numbers, underscores or hyphens")
                base = name.lower()
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
                if self.path.parent != self.directory or self.path.suffix != ".pt" or not self.path.is_file():
                    raise ValueError("Choose an existing checkpoint in models/")
                if not self.acquire():
                    raise ValueError("This model is already being trained in another session")
            else:
                raise ValueError("Choose resume, new or restart")

            if mode == "resume":
                self.env = AuraEnv()
                self.policy, data = load_checkpoint(self.path, self.env)
                self.run_id = data.get("training_run_id")
                self.optimizer = torch.optim.Adam(self.policy.parameters())
                self.optimizer.load_state_dict(data["optimizer"])
                self.episodes_completed = data["episodes_completed"]
                self.updates_completed = data.get("updates_completed", max(
                    (int(state.get("step", 0)) for state in self.optimizer.state.values()), default=0))
                self.config = replace(config, seed=data.get("training_seed", config.seed))
                if "rng_state" in data:
                    torch.set_rng_state(data["rng_state"])
                else:
                    torch.manual_seed(self.config.seed)
            else:
                self.env, self.policy, self.optimizer = create_training(config)
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
        )

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.release()
