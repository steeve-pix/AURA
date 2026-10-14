"""Run the real evaluation command twice; leave user checkpoints untouched."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pty
import select
import sys
import struct
import subprocess
import termios
import time

ROOT=Path(__file__).resolve().parents[1]


def run(command,path,cancel=False):
    master,slave=pty.openpty()
    fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack("HHHH",24,120,0,0))
    env=dict(os.environ,TERM="xterm-256color")
    env.pop("NO_COLOR",None)
    process=subprocess.Popen(["cli/build/aura-cli",command],cwd=ROOT,stdin=slave,stdout=slave,stderr=slave,env=env)
    os.close(slave)
    output=bytearray()
    def read():
        if select.select([master],[],[],.05)[0]:
            try: output.extend(os.read(master,65536))
            except OSError: pass
    def wait(text):
        deadline=time.monotonic()+45
        while text.encode() not in output:
            read()
            if time.monotonic()>deadline: raise AssertionError(output[-2500:])
    def send(keys):
        output.clear()
        os.write(master,keys)
    try:
        wait("EVALUATE MODEL")
        index=sorted((ROOT/"models").glob("*.pt")).index(path)
        send(b"j"*index+b"\r")
        wait("POLICY EVALUATION")
        wait("EVALUATING" if cancel else "COMPLETE")
        if not cancel:
            wait("No weight updates")
            fcntl.ioctl(master,termios.TIOCSWINSZ,struct.pack("HHHH",12,45,0,0))
            wait("Enlarge terminal")
            fcntl.ioctl(master,termios.TIOCSWINSZ,struct.pack("HHHH",24,120,0,0))
        send(b"q")
        wait("EVALUATE MODEL")
        send(b"q")
        deadline=time.monotonic()+8
        while process.poll() is None and time.monotonic()<deadline: read()
        assert process.poll()==0,output[-2000:]
    finally:
        if process.poll() is None: process.kill();process.wait()
        os.close(master)


def main():
    path=ROOT/"models"/f"zz_eval_test_{os.getpid()}.pt"
    history=path.parent/".evaluations"/(path.name+".json")
    sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]
    import torch
    from python.aura_env import AuraEnv
    from python.aura_policy import AuraPolicy
    from python.model_checkpoint import save_checkpoint
    torch.manual_seed(42)
    policy = AuraPolicy(AuraEnv().observation_size)
    save_checkpoint(policy, torch.optim.Adam(policy.parameters()), 0, path)
    original=hashlib.sha256(path.read_bytes()).digest()
    try:
        run("evaluate",path)
        first=json.loads(history.read_text())
        assert first["previous_steps"] is None
        run("eval",path)
        second=json.loads(history.read_text())
        assert second["steps"]==first["steps"]==second["previous_steps"]
        saved=history.read_bytes()
        run("evaluate",path,cancel=True)
        assert history.read_bytes()==saved
        assert hashlib.sha256(path.read_bytes()).digest()==original
        print(f"Evaluation CLI: {second['steps']} deterministic steps; alias, comparison, resize, cancellation and unchanged checkpoint passed")
    finally:
        path.unlink(missing_ok=True)
        history.unlink(missing_ok=True)


if __name__=="__main__": main()
