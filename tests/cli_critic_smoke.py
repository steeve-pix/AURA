"""Exercise real critic-only CLI selection, completion, resize and cancellation."""
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import shutil
import struct
import subprocess
import sys
import termios
import time

ROOT = Path(__file__).resolve().parents[1]


def run(actor, saved, cancel=False):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 160, 0, 0))
    process = subprocess.Popen(["cli/build/aura-cli", "train"], cwd=ROOT,
                               stdin=slave, stdout=slave, stderr=slave,
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
        deadline = time.monotonic()+60
        while text.encode() not in output:
            read()
            if time.monotonic()>deadline:
                raise AssertionError((text, output[:3000], output[-3000:]))

    def send(keys):
        output.clear()
        os.write(master, keys)

    try:
        wait("4  Critic only")
        send(b"4\r")
        wait("FROZEN ACTOR SOURCE")
        send(b"2\r" if saved else b"1\r")
        if saved:
            wait("FROZEN ACTOR")
            wait(actor.name)
            index = sorted((ROOT/"models").glob("*.pt")).index(actor)
            send(b"j"*index+b"\r")
        wait("CRITIC TRAINING LAB")
        if not cancel:
            wait("COMPLETE")
            wait("Actor change 0.00000000")
            output.clear()
            fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 16, 45, 0, 0))
            wait("Enlarge terminal")
            output.clear()
            fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 160, 0, 0))
            wait("COMPLETE")
        send(b"q")
        wait("CHOOSE HOW TO TRAIN")
        send(b"\x1b")
        deadline = time.monotonic()+8
        while process.poll() is None and time.monotonic()<deadline:
            read()
        assert process.poll()==0, output[-2000:]
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)


def main():
    sys.path[:0] = [str(ROOT), str(ROOT/"build-release")]
    import torch
    from python.aura_env import AuraEnv
    from python.aura_policy import AuraPolicy
    from python.model_checkpoint import save_checkpoint

    actor = ROOT/"models"/f"000_critic_test_{os.getpid()}.pt"
    artifacts = ROOT/"models"/"critics"
    torch.manual_seed(42)
    policy = AuraPolicy(AuraEnv().observation_size)
    save_checkpoint(policy, torch.optim.Adam(policy.parameters()), 0, actor)
    original = actor.read_bytes()
    before = set(artifacts.glob("critic-*/report.json"))
    owned = []
    try:
        run(actor, saved=False, cancel=True)
        assert set(artifacts.glob("critic-*/report.json"))==before
        run(actor, saved=True)
        for path in artifacts.glob("critic-*/report.json"):
            if path not in before:
                report = json.loads(path.read_text())
                if report["actor_source"]==str(actor):
                    owned.append(path.parent)
                    assert report["training_episodes"]==32 and report["validation_episodes"]==16
                    assert report["critic_updates"]==20
                    assert report["actor_max_parameter_change"]==0 and report["actor_has_no_gradients"]
                    checkpoint = torch.load(report["critic_path"], weights_only=True)
                    assert checkpoint["kind"]=="aura_value"
                    assert checkpoint["model"]["network.4.weight"].shape==(1, 64)
        assert len(owned)==1
        assert actor.read_bytes()==original
        print("Critic CLI: fresh-source cancellation, saved actor, 32/16 episodes, 20 updates, resize and separate artifacts passed; actor unchanged")
    finally:
        actor.unlink(missing_ok=True)
        # Clean up only reports produced for our uniquely named test actor.
        for path in artifacts.glob("critic-*/report.json"):
            if path not in before and json.loads(path.read_text()).get("actor_source")==str(actor):
                shutil.rmtree(path.parent)


if __name__=="__main__":
    main()
