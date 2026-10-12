"""Exercise the real Balance Lab in a PTY, including real OpenGL replays.

Run from any directory: python3 tests/cli_balance_smoke.py [--complete]
Requires a built cli/build/aura-cli and a graphical macOS session.
--complete also checks replay after all 21 training episodes finish.
"""

import argparse
import fcntl
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import termios
import time

ROOT = Path(__file__).resolve().parents[1]


class Dashboard:
    def __init__(self, rows=32):
        self.master, slave = pty.openpty()
        self.resize(rows, 120)
        command = [str(ROOT / "cli/build/aura-cli"), "train_balance"]
        self.process = subprocess.Popen(
            command, cwd=ROOT, stdin=slave, stdout=slave, stderr=slave,
            env={**os.environ, "TERM": "xterm-256color", "LANG": "en_US.UTF-8"},
            start_new_session=True,
        )
        os.close(slave)
        self.output = b""

    def resize(self, rows, columns):
        fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))

    def read(self):
        while select.select([self.master], [], [], 0)[0]:
            try:
                data = os.read(self.master, 65536)
            except OSError:
                break
            if not data:
                break
            self.output += data

    def wait(self, needle, since=0, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.read()
            if needle in self.output[since:]:
                return
            if self.process.poll() is not None:
                break
            time.sleep(0.025)
        raise AssertionError(f"Missing {needle!r}; last output: {self.output[-1800:]!r}")

    def send(self, data):
        self.read()
        mark = len(self.output)
        os.write(self.master, data)
        return mark

    def wait_exit(self, timeout=8):
        # Keep draining while the CLI restores its transcript; a PTY has a small
        # output buffer and wait() alone can block the child during final repaint.
        deadline = time.monotonic() + timeout
        while self.process.poll() is None and time.monotonic() < deadline:
            self.read()
            time.sleep(0.025)
        if self.process.poll() is None:
            raise subprocess.TimeoutExpired(self.process.args, timeout)

    def close(self):
        if self.process.poll() is None:
            self.send(b"q")
            try:
                self.wait_exit(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.wait_exit(timeout=5)
        os.close(self.master)


def check(complete=False):
    dashboard = Dashboard(rows=24 if complete else 32)
    try:
        dashboard.wait(b"BALANCE LAB")
        dashboard.wait(b"COMPLETE" if complete else b"Trial 1", timeout=240)
        # Mouse reporting must not exit, leak its payload, or change search state.
        dashboard.send(b"\x1b[<64;48;22M\x1b[<65;48;22M\x1b[A\x1b[B")
        time.sleep(0.2)
        assert dashboard.process.poll() is None
        mark = dashboard.send(b"\r")
        dashboard.wait(b"REPLAY", since=mark)
        dashboard.wait(b"Last replay:", since=mark)
        dashboard.wait(b"CURRENT EPISODE", since=mark)
        # Stop a second replay, then ensure the review session still responds.
        mark = dashboard.send(b"\x1b[A\r")
        dashboard.wait(b"REPLAY", since=mark)
        time.sleep(0.5)
        mark = dashboard.send(b"s")
        dashboard.wait(b"CURRENT EPISODE", since=mark)
        if not complete:
            mark = dashboard.send(b"sss")
            dashboard.wait(b"STOPPED", since=mark)
        # An idle review screen must not flood iTerm with cursor redraws.
        time.sleep(0.3)
        dashboard.read()
        idle_start = len(dashboard.output)
        time.sleep(0.6)
        dashboard.read()
        assert len(dashboard.output) == idle_start, "Idle dashboard emitted terminal redraws"
        dashboard.resize(20, 70)
        mark = dashboard.send(b"\r")
        dashboard.wait(b"REPLAY", since=mark)
        time.sleep(0.5)
        mark = dashboard.send(b"\x1b")
        dashboard.wait(b"Episode", since=mark)
        dashboard.resize(32, 120)
        dashboard.send(b"q")
        dashboard.wait_exit(timeout=8)
        assert dashboard.process.returncode == 0, dashboard.process.returncode
        print(f"{'Completed' if complete else 'Interrupted'} search and visual replay: passed", flush=True)
    finally:
        dashboard.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--complete", action="store_true")
    args = parser.parse_args()
    check()
    if args.complete:
        check(complete=True)
