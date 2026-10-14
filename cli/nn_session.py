"""CLI controls and telemetry for the training loop in train_nn.py."""

import argparse
import math
import os
import select
import sys
import time

import torch

from python.train_nn import (
    TrainingConfig,
    create_training,
    train_batch,
    evaluate_policy,
    DISCOUNT_FACTOR,
)
from python.model_checkpoint import save_checkpoint, promote_best_checkpoint
from cli.training_viewer import TrainingViewer
from cli.training_run import TrainingRun
from cli.model_viewer import emit_interface


class TrainingEnvironment:
    """Observe the environment from the CLI, without hooks in the training loop."""

    def __init__(self, env, session):
        self.env = env
        self.session = session
        self.episode = -1

    @property
    def episode_limit(self):
        return self.env.episode_limit

    def reset(self):
        self.session.service()
        self.episode += 1
        self.total_reward = 0.0
        return self.env.reset()

    def step(self, action):
        self.session.service()
        result = self.env.step(action)
        state, reward, terminated, truncated, info = result
        self.total_reward += reward
        self.session.progress(self.episode, info, action, reward, self.total_reward)
        if terminated or truncated:
            self.session.episode_finished(self.episode, info)
            if self.episode + 1 == self.session.config.batch_size:
                self.session.before_update()
        return result


class TrainingSession:
    """Own training state and report measurements without changing the physics."""

    def __init__(self, events=False, interactive=False, config=None, training=None):
        self.events = events
        self.interactive = interactive
        self.pending = ""
        self.paused = False
        self.next_batch = False
        self.selected = (0, 0)
        self.snapshots = []
        self.training = training
        self.best_initialized = False
        self.update = training.updates_completed if training else 0
        self.config = training.config if training else config or TrainingConfig()
        self.env, self.policy, self.optimizer = (training.env, training.policy, training.optimizer) if training else create_training(self.config)
        if training:
            self.emit("model", training.path.name, training.mode, training.episodes_completed, self.update)
        self.viewer = TrainingViewer(self.env.simulation, self.emit) if interactive else None
        self.parameters = list(self.policy.named_parameters())
        if interactive:
            os.set_blocking(sys.stdin.fileno(), False)
        self.emit("start", self.config.batch_size, self.env.episode_limit, self.config.seed, sum(p.numel() for _, p in self.parameters))
        group = self.optimizer.param_groups[0]
        self.emit("optimizer", group["lr"], *group["betas"], group["eps"])
        linear_names = [name for name, module in self.policy.named_modules() if isinstance(module, torch.nn.Linear)]
        labels = {name: ("Output" if i == len(linear_names) - 1 else f"Hidden {i + 1}")
                  for i, name in enumerate(linear_names)}
        for index, (name, parameter) in enumerate(self.parameters):
            module_name, _, field = name.rpartition(".")
            label = labels.get(module_name, module_name) + " / " + {"weight": "weights", "bias": "biases"}.get(field, field)
            self.emit("tensor", index, name, "x".join(map(str, parameter.shape)), parameter.numel(), label)
            self.emit("stats", index, parameter.detach().min().item(), parameter.detach().max().item(), parameter.detach().norm().item(), 0, 0)
        self.emit("device", next(self.policy.parameters()).device)
        modules = list(self.policy.network)
        for index, module in enumerate(modules):
            if isinstance(module, torch.nn.Linear):
                activation = type(modules[index + 1]).__name__ if index + 1 < len(modules) and not isinstance(modules[index + 1], torch.nn.Linear) else "Identity"
                self.emit("layer", module.in_features, module.out_features, activation)
        self.emit("sampling", self.policy.exploration_std, *self.policy.sample_bounds)
        self.emit("credit", DISCOUNT_FACTOR)
        import aura_python
        runtime = emit_interface(self.env, self.emit)
        specs = {spec["key"]: spec for spec in aura_python.action_specs}
        labels = {key: spec["label"] for key, spec in specs.items()}
        for key, (scale, nonnegative) in self.policy.action_transforms.items():
            self.emit("action", key, scale, int(nonnegative), labels.get(key, key))
            if key in specs and specs[key]["joint_name"] in runtime["motor_states"]:
                self.emit("motor", key, int(runtime["motor_states"][specs[key]["joint_name"]]))
        self.inspect()

    def emit(self, kind, *values):
        if self.events:
            print("AURA_NN", kind, *values, sep="\t", flush=True)

    def inspect(self):
        tensor, index = self.selected
        name, parameter = self.parameters[tensor]
        value = parameter.detach().flatten()[index].item()
        coordinates = f"{index // parameter.shape[1]},{index % parameter.shape[1]}" if parameter.ndim == 2 else str(index)
        if self.snapshots:
            snapshot = self.snapshots[tensor]
            before, gradient, moment, variance = (snapshot[key][index].item() for key in ("before", "gradient", "moment", "variance"))
            after = snapshot["after"][index].item()
            self.emit("inspect", tensor, index, coordinates, self.update, before, after, gradient, moment, variance)
        else:
            self.emit("inspect", tensor, index, coordinates, self.update, value, value, 0, 0, 0)

    def service(self):
        """Cooperate at simulation-step boundaries; pause retains the current graph."""
        if not self.interactive:
            return
        while True:
            while select.select([sys.stdin], [], [], 0)[0]:
                try:
                    data = os.read(sys.stdin.fileno(), 4096)
                except BlockingIOError:
                    break
                if not data:
                    raise KeyboardInterrupt
                self.pending += data.decode("ascii", errors="ignore")
            while "\n" in self.pending:
                command, self.pending = self.pending.split("\n", 1)
                parts = command.split()
                if command == "quit":
                    raise KeyboardInterrupt
                if command == "pause":
                    self.paused = not self.paused
                    self.emit("paused", int(self.paused))
                elif command == "next":
                    self.next_batch = True
                elif command == "visual":
                    self.viewer.toggle()
                elif len(parts) == 3 and parts[0] == "inspect":
                    try:
                        tensor, index = int(parts[1]), int(parts[2])
                        if 0 <= tensor < len(self.parameters) and 0 <= index < self.parameters[tensor][1].numel():
                            self.selected = tensor, index
                            self.inspect()
                    except ValueError:
                        pass
            self.pending = self.pending[-256:]
            self.viewer.service()
            if not self.paused:
                return
            time.sleep(0.04)

    def started(self):
        self.last_report = 0.0
        self.emit("phase", "collecting")

    def progress(self, episode, info, action, reward, total_reward):
        now = time.monotonic()
        if now - self.last_report >= 0.1 or info["terminated"] or info["truncated"]:
            self.emit("progress", self.update + 1, episode + 1, info["steps"], info["pelvis_height"], info["pelvis_uprightness"], *(action[key] for key in self.policy.action_transforms))
            self.emit("reward", self.update + 1, episode + 1, info["steps"], reward, total_reward, int(info["terminated"] or info["truncated"]))
            self.last_report = now

    def episode_finished(self, episode, info):
        self.emit("episode", self.update + 1, episode + 1, info["steps"], info["termination_reason"])

    def before_update(self):
        self.service()
        self.emit("phase", "optimizing")

    def updated(self, result, before):
        scores = result["episode_scores"]
        advantages = result["advantages"]
        returns = result["episode_returns"]
        episode_log_probs = result["episode_log_probabilities"]
        self.update += 1
        self.snapshots = []
        gradient_norm = math.sqrt(sum(parameter.grad.square().sum().item() for _, parameter in self.parameters))
        for index, ((name, parameter), old) in enumerate(zip(self.parameters, before)):
            state = self.optimizer.state[parameter]
            change = (parameter.detach() - old).abs().max().item()
            self.snapshots.append({
                "before": old.flatten(), "after": parameter.detach().clone().flatten(),
                "gradient": parameter.grad.detach().clone().flatten(),
                "moment": state["exp_avg"].clone().flatten(),
                "variance": state["exp_avg_sq"].clone().flatten(),
            })
            self.emit("stats", index, parameter.detach().min().item(), parameter.detach().max().item(), parameter.detach().norm().item(), parameter.grad.norm().item(), change)
        self.emit("update", self.update, result["loss"], sum(scores) / len(scores), max(scores), gradient_norm, result["max_parameter_change"])
        for episode, (score, advantage, log_prob, total_return) in enumerate(zip(scores, advantages, episode_log_probs, returns)):
            self.emit("objective", episode + 1, score, advantage, log_prob, total_return)
        self.inspect()

    def run(self):
        try:
            self.train()
        finally:
            if self.viewer is not None:
                self.viewer.close()

    def train(self):
        episodes_completed = 0
        if self.training and not getattr(self, "best_initialized", False):
            # Evaluate and preserve the starting policy before its first update.
            checkpoint = self.training.save_current()
            self.emit("checkpoint", checkpoint.name, self.training.episodes_completed)
            self.emit("phase", "evaluating")
            evaluation_steps = evaluate_policy(self.env, self.policy)
            self.emit("evaluation", self.update, evaluation_steps)
            best_path = promote_best_checkpoint(checkpoint, evaluation_steps)
            if best_path is None:
                best_path = checkpoint.with_name(f"{checkpoint.stem}_best.pt")
            best_score = torch.load(best_path, map_location="cpu", weights_only=True)["best_evaluation_steps"]
            self.emit("best", best_score, best_path.name)
            self.emit("phase", "ready")
            self.best_initialized = True
        while True:
            self.next_batch = False
            self.started()
            before = [parameter.detach().clone() for _, parameter in self.parameters]
            env = TrainingEnvironment(self.env, self)
            result = train_batch(env, self.policy, self.optimizer, self.config.batch_size)
            episodes_completed += len(result["episode_scores"])
            if self.training:
                checkpoint = self.training.save(len(result["episode_scores"]))
                completed = self.training.episodes_completed
            else:
                checkpoint = save_checkpoint(self.policy, self.optimizer, episodes_completed)
                completed = episodes_completed
            self.emit("checkpoint", checkpoint.name, completed)
            self.updated(result, before)
            self.emit("phase", "evaluating")
            evaluation_steps = evaluate_policy(self.env, self.policy)
            self.emit("evaluation", self.update, evaluation_steps)
            best_path = promote_best_checkpoint(checkpoint, evaluation_steps)
            if best_path is None:
                best_path = checkpoint.with_name(f"{checkpoint.stem}_best.pt")
            best_score = torch.load(best_path, map_location="cpu", weights_only=True)["best_evaluation_steps"]
            self.emit("best", best_score, best_path.name)
            self.emit("phase", "ready")
            if not self.events:
                print(f"{checkpoint.name} / {completed} episodes / {self.update} Adam updates\nCheckpoint saved: {checkpoint}", flush=True)
            if not self.interactive:
                return
            # Review remains responsive without advancing or pacing the simulator.
            while not self.next_batch:
                self.service()
                time.sleep(0.04)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events", action="store_true", help="Emit the CLI's tab-separated training telemetry")
    parser.add_argument("--interactive", action="store_true", help="Accept pause, next, visual, inspect and quit commands on stdin")
    parser.add_argument("--mode", choices=("resume", "new", "restart"), default="new")
    parser.add_argument("--name", default="aura_balance")
    parser.add_argument("--checkpoint")
    args = parser.parse_args()
    try:
        with TrainingRun(TrainingConfig(), args.mode, args.name, args.checkpoint) as training:
            TrainingSession(args.events, args.interactive, training=training).run()
    except KeyboardInterrupt:
        if args.events:
            print("AURA_NN\tstopped", flush=True)
        else:
            print("Training stopped.", flush=True)
    except Exception as error:
        parser.exit(1, f"Cannot start or save training: {error}\n")


if __name__ == "__main__":
    main()
