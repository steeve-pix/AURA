"""Interactive critic-only worker; reuse the controlled experiment, never train the actor."""
import argparse
import torch
import aura_python
import json
import os
import select
import sys
import tempfile
from pathlib import Path

from python.aura_env import AuraEnv, OBSERVATION_SIZE, OBSERVATION_SCHEMA_VERSION
from python.aura_policy import CONTROLLED_ACTION_KEYS
from python.experiments.critic_experiment import run_critic_experiment
from python.model_checkpoint import _write_checkpoint
from python.train_nn import CRITIC_RETURN_SCALE, DISCOUNT_FACTOR


class CriticSession:
    def __init__(self, emit, interactive=False, artifacts_dir=None):
        self.emit, self.interactive = emit, interactive
        self.artifacts_dir = Path(artifacts_dir) if artifacts_dir else Path(__file__).resolve().parents[2] / "models" / "critics"
        self.pending = ""
        self.phase = ""
        if interactive:
            os.set_blocking(sys.stdin.fileno(), False)

    def service(self):
        if not self.interactive:
            return
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
            if command == "quit":
                raise KeyboardInterrupt
        self.pending = self.pending[-256:]

    def progress(self, label, episode, total, steps):
        if self.phase != label:
            self.phase = label
            self.emit("phase", label)
        self.emit("progress", label, episode, total, steps)

    def measurement(self, row, baseline, constant, training_count, validation_count):
        if self.phase != "optimizing":
            self.phase = "optimizing"
            self.emit("baseline", baseline, constant, training_count, validation_count)
            self.emit("phase", self.phase)
        self.emit("metric", row["update"], row["training_mse"], row["validation_mse"])

    def save(self, report, critic, optimizer):
        self.service()
        self.artifacts_dir.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="critic-", dir=self.artifacts_dir))
        report_path, critic_path = directory / "report.json", directory / "critic.pt"
        report["environment_contract"] = getattr(self, "environment_contract", AuraEnv().environment_contract)
        report["report_path"], report["critic_path"] = str(report_path), str(critic_path)
        # Reuse the checkpoint writer's atomic replacement in a unique new directory.
        _write_checkpoint({"format_version": 1, "kind": "aura_value",
                           **report["environment_contract"], "model": critic.state_dict(), "optimizer": optimizer.state_dict(),
                           "observation_size": report["observation_size"],
                           "observation_schema_version": OBSERVATION_SCHEMA_VERSION,
                           "action_schema_version": report["action_schema"],
                           "physical_action_schema_version": aura_python.action_schema_version,
                           "return_scale": CRITIC_RETURN_SCALE, "discount_factor": DISCOUNT_FACTOR,
                           "updates_completed": report["critic_updates"], "experiment": report}, critic_path)
        with report_path.open("x") as output:
            json.dump(report, output, indent=2, allow_nan=False)
            output.write("\n")
        self.emit("saved", report_path, critic_path)

    def run(self, checkpoint=None):
        source = Path(checkpoint).name if checkpoint else "Fresh actor / seed 42"
        self.emit("start", source, OBSERVATION_SIZE, len(CONTROLLED_ACTION_KEYS), 32, 16, 20, 43, .001, DISCOUNT_FACTOR,
                  CRITIC_RETURN_SCALE)
        from python.independent_env import IndependentEnv
        from python.arena_env import checkpoint_environment
        factory = IndependentEnv
        if checkpoint:
            metadata = torch.load(checkpoint,map_location='cpu',weights_only=True)
            factory = lambda: checkpoint_environment(metadata)
        self.environment_contract = factory().environment_contract
        self.emit("objective", self.environment_contract.get("reward_name","unknown"), self.environment_contract.get("quality_version","legacy"), self.environment_contract.get("termination_mode","unknown"))
        report = run_critic_experiment(env_factory=factory, actor_checkpoint=checkpoint, progress=self.progress,
                                       measurement=self.measurement, service=self.service, on_complete=self.save)
        self.emit("result", report["actor_max_parameter_change"], int(report["actor_has_no_gradients"]),
                  report["critic_max_parameter_change"])
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", type=Path, help="Read-only actor snapshot; otherwise use a fresh seed-42 actor")
    parser.add_argument("--events", action="store_true")
    parser.add_argument("--interactive", action="store_true")
    parser.add_argument("--artifacts-dir", type=Path)
    args = parser.parse_args()

    def emit(kind, *values):
        if args.events:
            print("AURA_CRITIC", kind, *(str(value).replace("\t", " ").replace("\n", " ") for value in values),
                  sep="\t", flush=True)

    try:
        report = CriticSession(emit, args.interactive, args.artifacts_dir).run(args.checkpoint)
        if not args.events:
            print(json.dumps(report, indent=2, allow_nan=False))
    except KeyboardInterrupt:
        emit("stopped")
    except Exception as error:
        emit("error", str(error))
        parser.exit(1, f"Critic experiment failed: {error}\n")


if __name__ == "__main__":
    main()
