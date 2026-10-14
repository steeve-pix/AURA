"""Exercise Resume/New/Restart in the real terminal without replacing user models."""
import fcntl
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
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import torch
from python.aura_env import AuraEnv
from python.aura_policy import AuraPolicy
from python.model_checkpoint import save_checkpoint


class Console:
    def __init__(self):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
        env = dict(os.environ, TERM="xterm-256color")
        env.pop("NO_COLOR", None)
        self.process = subprocess.Popen(["cli/build/aura-cli", "train"], cwd=ROOT,
            stdin=slave, stdout=slave, stderr=slave, env=env)
        os.close(slave)
        self.output = bytearray()

    def read(self):
        if select.select([self.master], [], [], .05)[0]:
            try:
                self.output.extend(os.read(self.master, 65536))
            except OSError:
                pass

    def wait(self, text):
        deadline = time.monotonic() + 30
        while text.encode() not in self.output:
            self.read()
            if time.monotonic() > deadline:
                raise AssertionError(self.output[-2000:])

    def send(self, keys):
        self.output.clear()
        os.write(self.master, keys)

    def finish(self):
        deadline = time.monotonic() + 8
        while self.process.poll() is None and time.monotonic() < deadline:
            self.read()
        assert self.process.poll() == 0, self.output[-2000:]

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        os.close(self.master)


def main():
    directory = ROOT / "models"
    # z-prefix keeps this fixture last in the alphabetically sorted browser.
    name = f"zz_setup_test_{os.getpid()}"
    path = directory / (name + ".pt")
    env = AuraEnv()
    policy = AuraPolicy(env.observation_size, hidden_sizes=(5,))
    optimizer = torch.optim.Adam(policy.parameters())
    state, _ = env.reset()
    policy(state).square().sum().backward()
    optimizer.step()
    save_checkpoint(policy, optimizer, 9, path)
    original = path.read_bytes()
    try:
        for mode in ("resume", "new", "restart_cancel", "restart"):
            before_mode=path.read_bytes()
            console = Console()
            try:
                console.wait("TRAINING SETUP")
                console.send({"resume": b"1\r", "new": b"2\r"}.get(mode, b"3\r"))
                if mode == "new":
                    console.wait("Model name")
                    console.send(b"\x15" + name.encode() + b"\r")
                    expected = name + "_2.pt"
                else:
                    console.wait("CHECKPOINT LIBRARY")
                    def family_key(item):
                        stem=item.stem
                        return (stem[:-5] if stem.endswith('_best') else stem, stem.endswith('_best'), item.name)
                    index=sorted(directory.glob('*.pt'),key=family_key).index(path)
                    console.send(b"j" * index)
                    console.wait(name + ".pt")
                    console.send(b"\r")
                    expected = name + ".pt"
                    if mode.startswith("restart"):
                        console.wait("Reset weights")
                        console.send(b"n" if mode.endswith("cancel") else b"y")
                        if mode.endswith("cancel"):
                            console.wait("CHECKPOINT LIBRARY")
                            console.send(b"\x1b")
                            console.finish()
                            assert path.read_bytes() == before_mode
                            continue
                console.wait("NEURAL TRAINING LAB")
                console.send(b"p")
                console.wait("PAUSED")
                console.wait(expected)
                console.wait("9 episodes saved" if mode == "resume" else "0 episodes saved")
                console.send(b"q")
                console.wait("CHOOSE HOW TO TRAIN")
                console.send(b"\x1b")
                console.finish()
                # Initial evaluation may save metadata before pause is handled.
                # Resume must retain the exact actor weights; restart is explicitly
                # authorized to replace them. No optimizer update is requested.
                saved=torch.load(path,weights_only=True)
                initial=torch.load(__import__('io').BytesIO(original),weights_only=True)
                if mode != "restart":
                    for key,value in initial["model"].items():
                        torch.testing.assert_close(saved["model"][key],value,rtol=0,atol=0)
                assert saved["episodes_completed"] == (0 if mode == "restart" else 9)
            finally:
                console.close()
        print("CLI setup: resume counters, unique new name, restart confirmation/cancellation and paused exit passed")
    finally:
        for artifact in directory.glob(name+"*.pt"):
            artifact.unlink(missing_ok=True)
        for lock in directory.glob(f".{name}*.lock"):
            lock.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
