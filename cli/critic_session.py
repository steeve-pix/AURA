"""Interactive critic-only worker; reuse the controlled experiment, never train the actor."""
import argparse
import json
import os
from pathlib import Path
import select
import sys
import tempfile

from python.aura_env import OBSERVATION_SIZE, OBSERVATION_SCHEMA_VERSION
from python.aura_policy import CONTROLLED_ACTION_KEYS
from python.critic_experiment import run_critic_experiment
from python.model_checkpoint import _write_checkpoint
from python.train_nn import CRITIC_RETURN_SCALE, DISCOUNT_FACTOR


class CriticSession:
    def __init__(self, emit, interactive=False, artifacts_dir=None):
        self.emit, self.interactive = emit, interactive
        self.artifacts_dir = Path(artifacts_dir) if artifacts_dir else Path(__file__).resolve().parents[1]/"models"/"critics"
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
        report_path, critic_path = directory/"report.json", directory/"critic.pt"
        report["report_path"], report["critic_path"] = str(report_path), str(critic_path)
        # Reuse the checkpoint writer's atomic replacement in a unique new directory.
        _write_checkpoint({"format_version": 1, "kind": "aura_value",
                           "model": critic.state_dict(), "optimizer": optimizer.state_dict(),
                           "observation_size": report["observation_size"],
                           "observation_schema_version": OBSERVATION_SCHEMA_VERSION,
                           "return_scale": CRITIC_RETURN_SCALE, "discount_factor": DISCOUNT_FACTOR,
                           "updates_completed": report["critic_updates"], "experiment": report}, critic_path)
        with report_path.open("x") as output:
            json.dump(report, output, indent=2, allow_nan=False)
            output.write("\n")
        self.emit("saved", report_path, critic_path)

    def run(self, checkpoint=None):
        source = Path(checkpoint).name if checkpoint else "Fresh actor / seed 42"
        self.emit("start", source, OBSERVATION_SIZE, len(CONTROLLED_ACTION_KEYS), 32, 16, 20, 43, .001, DISCOUNT_FACTOR, CRITIC_RETURN_SCALE)
        report = run_critic_experiment(actor_checkpoint=checkpoint, progress=self.progress,
                                      measurement=self.measurement, service=self.service, on_complete=self.save)
        self.emit("result", report["actor_max_parameter_change"], int(report["actor_has_no_gradients"]), report["critic_max_parameter_change"])
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
            print("AURA_CRITIC", kind, *(str(value).replace("\t", " ").replace("\n", " ") for value in values), sep="\t", flush=True)

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
