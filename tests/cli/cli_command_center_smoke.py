"""Exercise the command center with a disposable policy; never train or change user models."""
import fcntl
import hashlib
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]
    import torch
    from python.aura_env import AuraEnv, OBSERVATION_SCHEMA_VERSION
    from python.aura_policy import AuraPolicy, AuraValue, ACTION_SCHEMA_VERSION
    from python.model_checkpoint import save_checkpoint
    from python.train_nn import evaluate_policy

    path = ROOT / "models" / f"000_center_test_{os.getpid()}.pt"
    history = path.parent / ".evaluations" / (path.name + ".json")
    torch.manual_seed(42)
    policy = AuraPolicy(AuraEnv().observation_size)
    # Compare CLI reporting against this physics build, not the historical
    # 377-step score from before the elbow-direction correction.
    expected_survival = evaluate_policy(AuraEnv(), policy)
    save_checkpoint(policy, torch.optim.Adam(policy.parameters()), 0, path)
    original = hashlib.sha256(path.read_bytes()).digest()
    import aura_python
    critic_dir = ROOT / "models" / "critics" / f"center_test_{os.getpid()}"
    critic_dir.mkdir()
    critic_path = critic_dir / "critic.pt"
    torch.save({"kind": "aura_value", "model": AuraValue(205).state_dict(),
                "observation_size": 205, "observation_schema_version": OBSERVATION_SCHEMA_VERSION,
                "action_schema_version": ACTION_SCHEMA_VERSION,
                "physical_action_schema_version": aura_python.action_schema_version,
                "return_scale": 100.0}, critic_path)
    critic_original = hashlib.sha256(critic_path.read_bytes()).digest()
    best_path = path.with_name(path.stem + "_best.pt")
    best_path.write_bytes(path.read_bytes())
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 140, 0, 0))
    process = subprocess.Popen(["cli/build/aura-cli"], cwd=ROOT, stdin=slave,
                               stdout=slave, stderr=slave,
                               env=dict(os.environ, TERM="xterm-256color"))
    os.close(slave)
    output = bytearray()

    def read():
        if select.select([master], [], [], .05)[0]:
            try:
                output.extend(os.read(master, 65536))
            except OSError:
                pass

    def wait(text):
        deadline = time.monotonic() + 60
        while text.encode() not in output:
            read()
            if time.monotonic() > deadline:
                raise AssertionError((text, output[:2500]))

    def send(keys):
        # A frame is painted row by row; allow the input loop to finish painting.
        time.sleep(.12)
        output.clear()
        os.write(master, keys)

    try:
        wait("SIMULATION WORKSPACE")
        wait("Actor: 205 inputs / 32 targets / Critic: 205 inputs / 1 value / separate")
        wait("Idle / no experiment running")
        assert b"workspace  " not in output
        assert b"branch     " not in output
        for alias in (b"/quit", b"quit"):
            send(b"\x15" + alias)
            wait("Close Aura CLI")
            wait(alias.decode())
            assert b"No matching command" not in output
        # Launch directly from the welcome screen, without submitting a command.
        send(b"\x15\x1b[D")
        wait("COMMAND CENTER")
        wait(path.name)
        send(b"/\x15" + path.name.encode() + b"\r")
        wait("1 / ")
        send(b"\r")
        wait("MODEL NETWORK")
        wait("OBSERVATIONS / 205 inputs")
        wait("MOVEMENTS / 32 outputs")
        send(b"\x1b[6~")
        send(b"\x1b[6~")
        wait("Right ankle tilt")
        send(b"q")
        wait("COMMAND CENTER")
        send(b"e")
        wait("POLICY EVALUATION")
        wait("COMPLETE")
        send(b"\x1b")
        wait("Center")
        wait("Finished / Evaluation / " + path.name)
        assert b"SIMULATION WORKSPACE" not in output
        # Clearing the transcript intentionally restores the welcome screen.
        send(b"/clear\r")
        wait("SIMULATION WORKSPACE")
        send(b"\x15!sh -c 'printf center_failure; exit 7'\r")
        wait("Failed")
        send(b"\x1b[D")
        wait("Loading models, experiments and designs / checking compatibility...")
        send(b"q")
        time.sleep(.2)
        send(b"/version\r")
        wait("Aura CLI")
        send(b"\x1b[D")
        wait("Loading models, experiments and designs / checking compatibility...")
        wait("COMMAND CENTER")
        wait(path.name)
        wait("0 saved episodes / 0 updates")
        send(b"/")
        wait("Type to filter")
        send(b"zz_no_matches_q")
        wait("No matching models")
        send(b"q")  # Q is text while searching, not Back.
        wait("zz_no_matches_qq")
        send(b"\x1b")  # Cancel the edit; restore the unfiltered library.
        wait("0 saved episodes / 0 updates")
        send(b"/\x15" + path.name.upper().encode() + b"\r")
        wait("1 / ")
        wait("Latest")
        send(b"b")  # Jump to a best copy excluded by the exact-name search.
        wait("Best checkpoint / read-only / watch or evaluate")
        wait("[T] Resume")  # Disabled keys stay visible in muted text.
        send(b"t")
        time.sleep(.2)
        read()
        assert b"RESUMING TRAINING" not in output and b"EXITED" not in output
        send(b"/\x15\x1b[200~" + path.stem.encode() + b"\x1b[201~")
        wait("2 / ")
        send(b"\r")
        send(b"/\x15" + path.name.encode() + b"\r")
        wait("1 / ")
        send(b"r")
        wait("Loading models, experiments and designs / checking compatibility...")
        output.clear()  # Wait for the refreshed frame, not buffered pre-refresh rows.
        wait("1 / ")
        send(b"2")
        wait("Saved experiments")
        send(b"3")
        wait("Saved model designs")
        send(b"1")
        wait("TYPE / MODEL")
        output.clear()
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 45, 0, 0))
        wait("Enlarge terminal")
        output.clear()
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 140, 0, 0))
        wait("TYPE / MODEL")
        send(b"e")
        wait("POLICY EVALUATION")
        wait("COMPLETE")
        send(b"q")
        wait("COMMAND CENTER")
        wait("TYPE / MODEL")
        output.clear()
        send(b"2")
        wait("Saved experiments")
        assert b"MODEL EVENT" not in output and b"MODEL LOG" not in output
        send(b"1")
        wait("TYPE / MODEL")
        send(b"\x1b")  # Esc skips the list and returns directly home.
        wait("Center")
        send(b"/version\r")
        wait("Aura CLI")
        send(b"/train\r")
        wait("CHOOSE HOW TO TRAIN")
        send(b"\r")
        wait("Checking saved models and compatibility...")
        wait("Your selection has been received")
        send(b"q")
        wait("CHOOSE HOW TO TRAIN")
        send(b"2\r")
        wait("NEW MODEL")
        send(b"\x15q")
        wait("q▏")  # Q remains editable text in the name field.
        send(b"\x1b")
        wait("CHOOSE HOW TO TRAIN")
        send(b"4\r")
        wait("FROZEN ACTOR SOURCE")
        send(b"q")
        wait("CHOOSE HOW TO TRAIN")
        send(b"4\r")
        wait("FROZEN ACTOR SOURCE")
        send(b"\x1b")  # Esc skips the parent setup.
        wait("Center")
        send(b"/center\r")
        wait("COMMAND CENTER")
        wait("MODEL DETAILS")
        wait("Latest")
        send(b"b")
        wait("Best checkpoint / read-only / watch or evaluate")
        wait(best_path.name)
        send(b"e")
        wait("POLICY EVALUATION")
        send(b"\x1b")
        wait("Center")
        send(b"/center\r")
        wait("COMMAND CENTER")
        send(b"/\x15critic " + critic_dir.name.encode() + b"\r")
        wait("Inspect")
        send(b"e")
        wait("Actor actions are unavailable")
        send(b"\r")
        wait("MODEL NETWORK")
        wait("CRITIC / return prediction")
        send(b"\x1b[6~")
        wait("PREDICTION / 1 output")
        send(b"q")
        wait("COMMAND CENTER")
        send(b"\r")
        wait("MODEL NETWORK")
        send(b"\x1b")
        wait("Center")
        send(b"/version\r")
        wait("Aura CLI")
        send(b"quit\r")
        deadline = time.monotonic() + 8
        while process.poll() is None and time.monotonic() < deadline:
            read()
        assert process.poll() == 0, output[-3000:]
        assert hashlib.sha256(path.read_bytes()).digest() == original
        assert hashlib.sha256(best_path.read_bytes()).digest() == original
        assert hashlib.sha256(critic_path.read_bytes()).digest() == critic_original
        print(
            "Command center: actor/critic network sheets and navigation, immediate loading, cancellation, Resume acknowledgement, model library, saved experiment/design tabs, resize, direct evaluation and return navigation passed; checkpoint unchanged")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        path.unlink(missing_ok=True)
        best_path.unlink(missing_ok=True)
        history.unlink(missing_ok=True)
        critic_path.unlink(missing_ok=True)
        critic_dir.rmdir()


if __name__ == "__main__":
    main()
