"""CLI controls and telemetry for the training loop in train_nn.py."""

import argparse
import copy
import math
import os
import select
import sys
import time
import threading
from pathlib import Path
import torch

from cli.backend.model_viewer import emit_interface
from cli.backend.training_run import TrainingRun
from cli.backend.training_viewer import TrainingViewer
from cli.backend.training_telemetry import RunProgress, device_metrics
from python.model_checkpoint import save_checkpoint, promote_best_checkpoint
from python.train_nn import (
    TrainingConfig,
    create_training,
    train_batch,
    evaluate_policy,
    DISCOUNT_FACTOR, create_critic, evaluate_balance,
)


def is_gpu_runtime_failure(error):
    message = str(error).lower()
    return any(marker in message for marker in (
        "mps", "metal", "cuda", "cudnn", "cublas", "out of memory", "device-side",
        "accelerator", "gpu runtime",
    ))


class TrainingEnvironment:
    """Observe the environment from the CLI, without hooks in the training loop."""

    def __init__(self, env, session):
        self.env = env
        self.session = session
        self.episode = -1

    def __getattr__(self, name):
        return getattr(self.env, name)

    def collect_batch(self, policy, episodes):
        last_report = {}
        self.session.emit("arena_spacing", self.env.spacing)
        def progress(episode, agent, info, action, reward, total):
            now = time.monotonic()
            if info['steps'] != 1 and not info['done'] and now - last_report.get(agent, 0.) < .1:
                return
            last_report[agent] = now
            if self.env.agents==1: self.session.progress(episode,info,action,reward,total)
            self.session.emit("arena_agent", agent + 1, info['steps'], info['balance_state'],
                              info['reward_qualities']['balance_quality'], total, int(info['done']), info['arena_contacts'])
            metrics = info['balance_metrics']
            self.session.emit("agent_detail", agent + 1, episode + 1, info['pelvis_height'],
                              info['pelvis_uprightness'], reward,
                              *self.session.balance_fields(metrics),
                              *(action[key] for key in policy.action_transforms))
        def finished(episode, info, action, reward, total):
            self.session.emit("agent_result", info.get("worker_id", episode % self.env.agents) + 1, episode + 1,
                              *self.session.balance_fields(info['balance_metrics']))
            self.session.emit("progress", self.session.update + 1, episode + 1, info['steps'],
                              info['pelvis_height'], info['pelvis_uprightness'],
                              *(action[key] for key in policy.action_transforms))
            self.session.emit("reward", self.session.update + 1, episode + 1, info['steps'], reward, total, 1)
            self.session.episode_finished(episode, info)
        result = self.env.collect_batch(policy, episodes, service=self.session.service,
                                        progress=progress, finished=finished)
        self.session.before_update()
        return result

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
        self.run_clock = RunProgress()
        self.device_info = False
        self.device_monitor_stop = threading.Event()
        self.device_monitor_thread = None
        self.telemetry_lock = threading.Lock()
        self.gpu_updates = 0
        self.last_timing_report = -math.inf
        self.telemetry_phase = 'starting'
        self.training = training
        self.best_initialized = False
        self.batch_metrics = {}
        self.last_evaluation = {}
        self.update = training.updates_completed if training else 0
        self.config = training.config if training else config or TrainingConfig()
        self.env, self.policy, self.optimizer = (training.env, training.policy,
                                                 training.optimizer) if training else create_training(self.config)
        self.critic, self.critic_optimizer = ((training.critic, training.critic_optimizer) if training
                                             else create_critic(self.env, self.config))
        self.emit("collector", "independent" if getattr(getattr(self,"env",None),"is_independent",False) else "historical")
        self.emit("objective_contract", "balance_quality", 3, "balance_v2", 2, "actor_critic_mc")
        self.env.runtime_service = self.service if interactive else None
        self.emit("schedule", self.config.batch_size, getattr(self.config, 'batches', 1), getattr(self.config, 'agents', 1))
        if training:
            self.emit("model", training.path.name, training.mode, training.episodes_completed, self.update)
        self.viewer = TrainingViewer((self.env if getattr(getattr(self,"env",None),"is_independent",False) else self.env.simulation), self.emit) if interactive else None
        self.parameters = list(self.policy.named_parameters())
        if interactive:
            os.set_blocking(sys.stdin.fileno(), False)
        self.emit("start", self.config.batch_size, self.env.episode_limit, self.config.seed,
                  sum(p.numel() for _, p in self.parameters))
        group = self.optimizer.param_groups[0]
        self.emit("optimizer", group["lr"], *group["betas"], group["eps"])
        linear_names = [name for name, module in self.policy.named_modules() if isinstance(module, torch.nn.Linear)]
        labels = {name: ("Output" if i == len(linear_names) - 1 else f"Hidden {i + 1}")
                  for i, name in enumerate(linear_names)}
        for index, (name, parameter) in enumerate(self.parameters):
            module_name, _, field = name.rpartition(".")
            label = labels.get(module_name, module_name) + " / " + {"weight": "weights", "bias": "biases"}.get(field,
                                                                                                               field)
            self.emit("tensor", index, name, "x".join(map(str, parameter.shape)), parameter.numel(), label)
            self.emit("stats", index, parameter.detach().min().item(), parameter.detach().max().item(),
                      parameter.detach().norm().item(), 0, 0)
        self.emit("device", next(self.policy.parameters()).device)
        if getattr(getattr(self,"env",None),"is_independent",False):
            self.emit("device_metric","rollout_inference","CPU / frozen actor in isolated workers")
        modules = list(self.policy.network)
        for index, module in enumerate(modules):
            if isinstance(module, torch.nn.Linear):
                activation = type(modules[index + 1]).__name__ if index + 1 < len(modules) and not isinstance(
                    modules[index + 1], torch.nn.Linear) else "Identity"
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
        if training:
            self.restore_history(training.history)

    def restore_history(self, history):
        """Restore chart data without replaying live optimizer or episode events."""
        for row in history:
            d = row.get("deterministic", {})
            self.emit("history_update", row["update"], row.get("actor_loss", 0.),
                      row.get("mean_survival", 0.), d.get("steps", -1), d.get("balanced_seconds", -1.), int("actor_loss" in row))
            for episode in row.get("episodes", []):
                if "worker_id" in episode:
                    self.emit("worker_history", episode["worker_id"]+1, episode["index"], episode["steps"], episode.get("balanced_seconds",0.))
                self.emit("history_episode", episode["index"], episode["steps"], episode["reward"],
                          episode.get("balanced_seconds", -1.))
        if history and "actor_loss" in history[-1]:
            last = history[-1]
            self.emit("history_state", last['actor_loss'], last['critic_loss'],
                      last['gradient_norm'], last['max_parameter_change'])
        self.emit("history_end", self.training.episodes_completed, len(history))

    def record_history(self, result=None):
        if not self.training:
            return
        row = dict(update=self.update, episodes_completed=self.training.episodes_completed,
                   device=str(next(self.policy.parameters()).device),
                   deterministic=self.last_evaluation.get("deterministic", {}),
                   stochastic=self.last_evaluation.get("stochastic", {}), episodes=[])
        if result is not None:
            row.update(actor_loss=result["loss"], critic_loss=result["critic_loss"],
                       gradient_norm=math.sqrt(sum(p.grad.square().sum().item() for _, p in self.parameters)),
                       max_parameter_change=result["max_parameter_change"],
                       mean_survival=sum(result["episode_scores"]) / len(result["episode_scores"]))
            offset = self.training.episodes_completed - len(result["episode_scores"])
            for i, (steps, reward) in enumerate(zip(result["episode_scores"], result["episode_returns"])):
                metrics = self.batch_metrics.get(i, {})
                row["episodes"].append(dict(index=offset+i+1, steps=steps,
                    reward=metrics.get("total_reward", reward), **{k:v for k,v in metrics.items() if k != "steps" and k != "total_reward"}))
        # An initial evaluation on resume must not replace the recorded update.
        if not any(old["update"] == self.update for old in self.training.history):
            self.training.history.append(row)
        self.training.save_current()

    def emit(self, kind, *values):
        if kind == 'phase':
            self.telemetry_phase = values[0]
        if self.events:
            with self.telemetry_lock:
                print("AURA_NN", kind, *values, sep="\t", flush=True)

    def report_telemetry(self, force=False):
        now = time.monotonic()
        if self.run_clock.start_time is not None and (force or now - self.last_timing_report >= .5):
            self.emit('run_timing', *self.run_clock.snapshot())
            self.last_timing_report = now

    def set_device_info(self, enabled):
        self.device_info = enabled
        self.device_monitor_stop.set()
        if self.device_monitor_thread is not None:
            self.device_monitor_thread.join(timeout=1.5)
        self.device_monitor_thread = None
        if not enabled:
            return
        self.device_monitor_stop = threading.Event()
        stop = self.device_monitor_stop
        def sample():
            while not stop.is_set():
                try:
                    values = device_metrics(self.policy, self.critic, self.gpu_updates)
                    if getattr(getattr(self,"env",None),"is_independent",False):
                        values["rollout_inference"] = "CPU / frozen actor per worker"
                        values["neural"] = str(next(self.policy.parameters()).device)+" / gradients and optimizer updates between batches"
                    values['phase'] = self.telemetry_phase + (' / paused' if self.paused else '')
                    for key, value in values.items():
                        self.emit('device_metric', key, value)
                except (RuntimeError, OSError):
                    self.emit('device_metric', 'utilization', 'Measurement unavailable')
                if stop.wait(2.):
                    break
        self.device_monitor_thread = threading.Thread(target=sample, daemon=True, name='aura-device-monitor')
        self.device_monitor_thread.start()

    def inspect(self):
        tensor, index = self.selected
        name, parameter = self.parameters[tensor]
        value = parameter.detach().flatten()[index].item()
        coordinates = f"{index // parameter.shape[1]},{index % parameter.shape[1]}" if parameter.ndim == 2 else str(
            index)
        if self.snapshots:
            snapshot = self.snapshots[tensor]
            before, gradient, moment, variance = (snapshot[key][index].item() for key in
                                                  ("before", "gradient", "moment", "variance"))
            after = snapshot["after"][index].item()
            self.emit("inspect", tensor, index, coordinates, self.update, before, after, gradient, moment, variance)
        else:
            self.emit("inspect", tensor, index, coordinates, self.update, value, value, 0, 0, 0)

    def service(self):
        """Cooperate at simulation-step boundaries; pause retains the current graph."""
        self.report_telemetry()
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
                    if hasattr(self.env,"set_paused"): self.env.set_paused(self.paused)
                    self.run_clock.pause(self.paused)
                    self.emit("paused", int(self.paused))
                    self.report_telemetry(force=True)
                elif len(parts) == 2 and parts[0] == 'device_info' and parts[1] in ('0', '1'):
                    self.set_device_info(parts[1] == '1')
                elif len(parts)==2 and parts[0]=="worker":
                    self.env.selected_worker = max(0,min(self.config.agents-1,int(parts[1])))
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
            self.report_telemetry()
            self.viewer.service()
            if not self.paused:
                return
            time.sleep(0.04)

    def started(self):
        self.batch_metrics = {}
        self.last_report = 0.0
        self.run_clock.episodes = 0
        self.emit("phase", "collecting")

    def progress(self, episode, info, action, reward, total_reward):
        now = time.monotonic()
        if now - self.last_report >= 0.1 or info["terminated"] or info["truncated"]:
            self.emit("progress", self.update + 1, episode + 1, info["steps"], info["pelvis_height"],
                      info["pelvis_uprightness"], *(action[key] for key in self.policy.action_transforms))
            self.emit("reward", self.update + 1, episode + 1, info["steps"], reward, total_reward,
                      int(info["terminated"] or info["truncated"]))
            self.last_report = now

    @staticmethod
    def balance_fields(metrics):
        return (metrics['balanced_steps'], metrics['balanced_seconds'], metrics['unbalanced_steps'],
                metrics['longest_balanced_steps'], metrics['recoveries'], metrics['fall_rate'],
                metrics['average_balance_quality'], metrics['total_reward'], metrics['termination_reason'])

    def emit_balance(self, label, metrics):
        self.emit("balance", label, *self.balance_fields(metrics))

    def evaluate(self):
        try:
            deterministic = evaluate_balance(self.env, self.policy)
            stochastic = evaluate_balance(self.env, self.policy, stochastic=True, seed=self.config.seed + 10000)
        except RuntimeError as error:
            if not self._is_gpu_runtime_failure(error):
                raise
            self._switch_to_cpu(str(error))
            deterministic = evaluate_balance(self.env, self.policy)
            stochastic = evaluate_balance(self.env, self.policy, stochastic=True, seed=self.config.seed + 10000)
        self.last_evaluation = dict(deterministic=deterministic, stochastic=stochastic)
        self.emit_balance("deterministic", deterministic)
        self.emit_balance("stochastic", stochastic)
        self.emit("evaluation", self.update, deterministic['steps'])
        return deterministic

    @staticmethod
    def _cpu_snapshot(value):
        if isinstance(value, torch.Tensor):
            return value.detach().cpu().clone()
        if isinstance(value, dict):
            return {key: TrainingSession._cpu_snapshot(item) for key, item in value.items()}
        if isinstance(value, list):
            return [TrainingSession._cpu_snapshot(item) for item in value]
        if isinstance(value, tuple):
            return tuple(TrainingSession._cpu_snapshot(item) for item in value)
        return copy.deepcopy(value)

    def _is_gpu_runtime_failure(self, error):
        if self.config.device != "gpu" or getattr(self, "cpu_fallback_used", False):
            return False
        return is_gpu_runtime_failure(error)

    def _switch_to_cpu(self, reason, snapshot=None):
        """Move the whole learning state to CPU; remain there for this run."""
        self.policy.to("cpu")
        if hasattr(self.critic, "to"):
            self.critic.to("cpu")
        if snapshot is not None:
            self.policy.load_state_dict(snapshot["actor"], strict=True)
            if snapshot["critic"] is not None:
                self.critic.load_state_dict(snapshot["critic"], strict=True)
            if snapshot["actor_optimizer"] is not None:
                self.optimizer.load_state_dict(snapshot["actor_optimizer"])
            if snapshot["critic_optimizer"] is not None:
                self.critic_optimizer.load_state_dict(snapshot["critic_optimizer"])
        for optimizer in (self.optimizer, self.critic_optimizer):
            if not hasattr(optimizer, "state"):
                continue
            for state in optimizer.state.values():
                for key, value in state.items():
                    if isinstance(value, torch.Tensor):
                        state[key] = value.detach().cpu()
        from dataclasses import replace
        self.config = replace(self.config,device="cpu")
        if self.training:
            self.training.config = self.config
        self.cpu_fallback_used = True
        detail = " ".join(reason.split())
        if len(detail) > 180:
            detail = detail[:177] + "..."
        self.emit("device", "cpu")
        self.emit("warning", f"GPU error; switched to CPU for this run. {detail}")

    def train_batch_with_fallback(self, env):
        """Retry one failed GPU batch on CPU without committing partial updates."""
        snapshot = {
            "actor": self._cpu_snapshot(self.policy.state_dict()),
            "actor_optimizer": self._cpu_snapshot(self.optimizer.state_dict()),
            "critic": self._cpu_snapshot(self.critic.state_dict()) if hasattr(self.critic, "state_dict") else None,
            "critic_optimizer": self._cpu_snapshot(self.critic_optimizer.state_dict())
            if hasattr(self.critic_optimizer, "state_dict") else None,
        }
        batch_index = getattr(env, "batch_index", None)
        rng_state = torch.get_rng_state()
        before = [parameter.detach().clone() for _, parameter in self.parameters]
        try:
            result = train_batch(env, self.policy, self.optimizer, self.config.batch_size,
                                 critic=self.critic, critic_optimizer=self.critic_optimizer)
        except RuntimeError as error:
            if not self._is_gpu_runtime_failure(error):
                raise
            self._switch_to_cpu(str(error), snapshot)
            torch.set_rng_state(rng_state)
            if batch_index is not None: env.batch_index = batch_index
            self.emit("phase", "collecting")
            self.run_clock.episodes = 0
            self.batch_metrics = {}
            before = [parameter.detach().clone() for _, parameter in self.parameters]
            result = train_batch(env, self.policy, self.optimizer, self.config.batch_size,
                                 critic=self.critic, critic_optimizer=self.critic_optimizer)
        return result, before

    def episode_finished(self, episode, info):
        self.run_clock.episodes += 1
        if "balance_metrics" in info:
            self.batch_metrics[episode] = dict(info["balance_metrics"])
            self.emit_balance("training", info["balance_metrics"])
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
        if next(self.policy.parameters()).device.type in ('mps', 'cuda') and \
                next(self.critic.parameters()).device.type in ('mps', 'cuda'):
            self.gpu_updates += 1
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
            self.emit("stats", index, parameter.detach().min().item(), parameter.detach().max().item(),
                      parameter.detach().norm().item(), parameter.grad.norm().item(), change)
        self.emit("update", self.update, result["loss"], sum(scores) / len(scores), max(scores), gradient_norm,
                  result["max_parameter_change"])
        for episode, (score, advantage, log_prob, total_return) in enumerate(
                zip(scores, advantages, episode_log_probs, returns)):
            self.emit("objective", episode + 1, score, advantage, log_prob, total_return)
        self.inspect()

    def run(self):
        try:
            self.train()
        finally:
            self.set_device_info(False)
            if self.viewer is not None:
                self.viewer.close()

    def train(self):
        episodes_completed = 0
        if self.training and not getattr(self, "best_initialized", False):
            # Evaluate and preserve the starting policy before its first update.
            checkpoint = self.training.save_current()
            self.emit("checkpoint", checkpoint.name, self.training.episodes_completed)
            self.emit("phase", "evaluating")
            metrics = self.evaluate()
            self.record_history()
            evaluation_steps = metrics['steps']
            best_path = promote_best_checkpoint(checkpoint, evaluation_steps, balance_metrics=metrics)
            if best_path is None:
                best_path = checkpoint.with_name(f"{checkpoint.stem}_best.pt")
            saved_best = torch.load(best_path, map_location="cpu", weights_only=True)
            best_score = saved_best["best_evaluation_steps"]
            if 'best_balance_metrics' in saved_best:
                self.emit_balance('best', saved_best['best_balance_metrics'])
            self.emit("best", best_score, best_path.name)
            self.emit("phase", "ready")
            self.best_initialized = True
        while True:
            self.run_clock.start(getattr(self.config, 'batches', 1))
            for run_batch in range(getattr(self.config, 'batches', 1)):
                self.emit("run_progress", run_batch + 1, getattr(self.config, 'batches', 1), getattr(self.config, 'agents', 1))
                self.next_batch = False
                self.started()
                env = TrainingEnvironment(self.env, self)
                result, before = self.train_batch_with_fallback(env)
                self.emit("critic_update", result['critic_loss'])
                episodes_completed += len(result["episode_scores"])
                if self.training:
                    checkpoint = self.training.save(len(result["episode_scores"]))
                    completed = self.training.episodes_completed
                else:
                    checkpoint = save_checkpoint(self.policy, self.optimizer, episodes_completed,
                                                 critic=self.critic, critic_optimizer=self.critic_optimizer)
                    completed = episodes_completed
                self.emit("checkpoint", checkpoint.name, completed)
                self.updated(result, before)
                self.emit("phase", "evaluating")
                metrics = self.evaluate()
                self.record_history(result)
                evaluation_steps = metrics['steps']
                best_path = promote_best_checkpoint(checkpoint, evaluation_steps, balance_metrics=metrics)
                if best_path is None:
                    best_path = checkpoint.with_name(f"{checkpoint.stem}_best.pt")
                saved_best = torch.load(best_path, map_location="cpu", weights_only=True)
                best_score = saved_best["best_evaluation_steps"]
                if 'best_balance_metrics' in saved_best:
                    self.emit_balance('best', saved_best['best_balance_metrics'])
                self.emit("best", best_score, best_path.name)
                self.run_clock.finish_update()
                self.report_telemetry(force=True)
                self.emit("phase", "ready" if run_batch + 1 == getattr(self.config, 'batches', 1) else "collecting")
                if not self.events:
                    print(
                        f"{checkpoint.name} / {completed} episodes / {self.update} Adam updates\nCheckpoint saved: {checkpoint}",
                        flush=True)
            if not self.interactive:
                return
            # Review remains responsive without advancing or pacing the simulator.
            while not self.next_batch:
                self.service()
                time.sleep(0.04)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events", action="store_true", help="Emit the CLI's tab-separated training telemetry")
    parser.add_argument("--interactive", action="store_true",
                        help="Accept pause, next, visual, inspect and quit commands on stdin")
    parser.add_argument("--mode", choices=("resume", "new", "restart", "fork"), default="new")
    parser.add_argument("--name", default="aura_balance")
    parser.add_argument("--checkpoint")
    parser.add_argument("--episodes-per-batch", type=int, default=32)
    parser.add_argument("--batches", type=int, default=3, help="Consecutive updates before review; N repeats this block")
    parser.add_argument("--agents", type=int, default=None,
                        help="Independent physics workers; editable on resume")
    parser.add_argument("--device", choices=("cpu", "gpu"), default="cpu",
                        help="CPU or an available CUDA/Apple Metal GPU")
    parser.add_argument("--template", type=Path, help="Saved model design for fresh training")
    args = parser.parse_args()
    try:
        agents = args.agents
        if agents is None:
            from python.worker_config import default_workers
            agents = default_workers()
        config = TrainingConfig(batch_size=args.episodes_per_batch, batches=args.batches, agents=agents,
                                device=args.device)
        from python.worker_config import worker_warning
        warning = worker_warning(config.agents)
        if warning: print("AURA_NN\twarning\t"+warning,flush=True)
        from python.model_templates import TEMPLATE_DIR, load_template, NetworkDesign
        template_path = args.template
        default_path = TEMPLATE_DIR / 'default.json'
        if template_path is None and args.mode == 'new' and default_path.exists():
            import json
            template_path = Path(json.loads(default_path.read_text())['template'])
        if template_path and args.mode != 'new':
            raise ValueError('Templates initialize fresh models; resume uses the saved network design')
        if template_path:
            design = load_template(template_path)
            from dataclasses import replace
            config = replace(config, actor_design=NetworkDesign(**design['actor']),critic_design=NetworkDesign(**design['critic']))
        try:
            training_run = TrainingRun(config, args.mode, args.name, args.checkpoint)
        except RuntimeError as error:
            if config.device != "gpu" or not is_gpu_runtime_failure(error):
                raise
            from dataclasses import replace
            config = replace(config,device="cpu")
            if args.events:
                print(f"AURA_NN\twarning\tGPU unavailable during setup; starting on CPU. {str(error)}", flush=True)
                print("AURA_NN\tdevice\tcpu", flush=True)
            else:
                print(f"GPU unavailable during setup; starting on CPU. {error}", flush=True)
            training_run = TrainingRun(config, args.mode, args.name, args.checkpoint)
        with training_run as training:
            TrainingSession(args.events, args.interactive, training=training).run()
    except KeyboardInterrupt:
        if args.events:
            print("AURA_NN\tstopped", flush=True)
        else:
            print("Training stopped.", flush=True)
    except Exception as error:
        message = " ".join(str(error).split()) or "No additional details were provided."
        if args.events:
            print(f"AURA_NN\terror\t{type(error).__name__}: {message}", flush=True)
            raise SystemExit(1)
        parser.exit(1, f"Training failed ({type(error).__name__}): {message}\n")


if __name__ == "__main__":
    main()
