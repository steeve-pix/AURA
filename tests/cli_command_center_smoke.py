"""Exercise the command center with a disposable policy; never train or change user models."""
import fcntl
import hashlib
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import sys
import termios
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]
    import torch
    from python.aura_env import AuraEnv
    from python.aura_policy import AuraPolicy
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
    best_path=path.with_name(path.stem+"_best.pt")
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
        wait("Actor: REINFORCE / Critic: independent")
        wait("Idle / no experiment running")
        assert b"workspace  " not in output
        assert b"branch     " not in output
        for alias in (b"/quit", b"quit"):
            send(b"\x15"+alias)
            wait("Close Aura CLI")
            wait(alias.decode())
            assert b"No matching command" not in output
        # Launch directly from the welcome screen, without submitting a command.
        send(b"\x15\x1b[D")
        wait("COMMAND CENTER")
        wait(path.name)
        send(b"e")
        wait("POLICY EVALUATION")
        wait("COMPLETE")
        send(b"\x1b")
        wait("Models")
        wait("Finished / Evaluation / " + path.name)
        assert b"SIMULATION WORKSPACE" not in output
        # Clearing the transcript intentionally restores the welcome screen.
        send(b"/clear\r")
        wait("SIMULATION WORKSPACE")
        send(b"\x15!sh -c 'printf center_failure; exit 7'\r")
        wait("Failed")
        send(b"\x1b[D")
        wait("Checking saved models and compatibility...")
        send(b"q")
        time.sleep(.2)
        send(b"/version\r")
        wait("Aura CLI")
        send(b"\x1b[D")
        wait("Checking saved models and compatibility...")
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
        send(b"/\x15"+path.name.upper().encode()+b"\r")
        wait("1 / ")
        wait("Latest")
        send(b"b")  # Jump to a best copy excluded by the exact-name search.
        wait("Best checkpoint / saved evaluation winner")
        send(b"/\x15\x1b[200~"+path.stem.encode()+b"\x1b[201~")
        wait("2 / ")
        send(b"\r")
        send(b"/\x15"+path.name.encode()+b"\r")
        wait("1 / ")
        send(b"r")
        wait("Checking saved models and compatibility...")
        output.clear()  # Wait for the refreshed frame, not buffered pre-refresh rows.
        wait("1 / ")
        send(b"2")
        wait("No model events in this view.")
        assert b"center_failure" not in output
        assert b"Exit 7" not in output
        for keys,label in ((b"\x1bOC","Training"),(b"\x1b[C","Evaluation"),
                           (b"\x1bOD","Training"),(b"\x1b[D","All"),
                           (b"\t","Training"),(b"\x1b[Z","All")):
            send(keys)
            wait("\x1b[7m " + label + " 0 ")
        send(b"1")
        wait("VERSION / MODEL")
        output.clear()
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 45, 0, 0))
        wait("Enlarge terminal")
        output.clear()
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 140, 0, 0))
        wait("VERSION / MODEL")
        send(b"e")
        wait("POLICY EVALUATION")
        wait("COMPLETE")
        send(b"q")
        wait("COMMAND CENTER")
        wait("MODEL LOG / current session")
        wait("Finished / Evaluation / " + path.name)
        send(b"2")
        wait("Evaluation / " + path.name)
        wait("Deterministic survival:")
        wait(f"{expected_survival} steps")
        assert b"Simulation build / Release" not in output
        assert b"center_failure" not in output
        send(b"\r")
        wait("MODEL EVENT DETAILS")
        for back_key in (b"q", b"Q"):
            send(back_key)
            wait("MODEL DETAILS")
            send(b"\r")
            wait("MODEL EVENT DETAILS")
        send(b"\x1b")  # Esc skips the list and returns directly home.
        wait("Models")
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
        wait("Models")
        send(b"/center\r")
        wait("COMMAND CENTER")
        wait("MODEL DETAILS")
        wait("Latest")
        send(b"b")
        wait("Best checkpoint / saved evaluation winner")
        wait(best_path.name)
        send(b"e")
        wait("POLICY EVALUATION")
        send(b"\x1b")
        wait("Models")
        send(b"/version\r")
        wait("Aura CLI")
        send(b"quit\r")
        deadline = time.monotonic() + 8
        while process.poll() is None and time.monotonic() < deadline:
            read()
        assert process.poll() == 0, output[-3000:]
        assert hashlib.sha256(path.read_bytes()).digest() == original
        assert hashlib.sha256(best_path.read_bytes()).digest() == original
        print("Command center: immediate loading, cancellation, Resume acknowledgement, model library, shell/build exclusion from model logs, resize, direct evaluation, result logs and return navigation passed; checkpoint unchanged")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        path.unlink(missing_ok=True)
        best_path.unlink(missing_ok=True)
        history.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
