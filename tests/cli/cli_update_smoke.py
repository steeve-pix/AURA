"""Test /update in a real PTY using an older build and isolated install folders.

Usage: python3 tests/cli/cli_update_smoke.py --old-binary /path/to/previous/aura-cli
The older build must already support /update and have an earlier source build ID.
No training, model writes, Git operations or source edits occur.
"""
import argparse
import fcntl
import os
import pty
import re
import select
import shutil
import struct
import subprocess
import tempfile
import termios
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ANSI = re.compile(rb'\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b\][^\x07]*(?:\x07)')


class Console:
    def __init__(self, binary, cwd, env):
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', 36, 140, 0, 0))
        self.original = termios.tcgetattr(self.slave)
        self.process = subprocess.Popen([str(binary)], cwd=cwd, env=env, stdin=self.slave, stdout=self.slave,
                                        stderr=self.slave)
        self.output = b''

    def wait(self, text, timeout=30):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            decoded = ANSI.sub(b'', self.output).decode(errors='replace')
            if text in decoded: return decoded
            if self.process.poll() is not None: raise AssertionError(f'CLI exited early: {decoded[-2000:]}')
            if select.select([self.master], [], [], .1)[0]:
                try:
                    self.output += os.read(self.master, 65536)
                except OSError:
                    break
        raise AssertionError(f'Missing {text!r}: {ANSI.sub(b"", self.output).decode(errors="replace")[-3000:]}')

    def send(self, data):
        os.write(self.master, data)

    def finish(self):
        self.send(b'/exit\r')
        until = time.monotonic() + 10
        while self.process.poll() is None and time.monotonic() < until:
            if select.select([self.master], [], [], .1)[0]:
                try:
                    self.output += os.read(self.master, 65536)
                except OSError:
                    break
        assert self.process.poll() is not None, ANSI.sub(b'', self.output).decode(errors='replace')[-3000:]
        assert self.process.returncode == 0
        mask = termios.ICANON | termios.ECHO
        assert termios.tcgetattr(self.slave)[3] & mask == self.original[3] & mask

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            until = time.monotonic() + 3
            while self.process.poll() is None and time.monotonic() < until:
                if select.select([self.master], [], [], .1)[0]:
                    try:
                        os.read(self.master, 65536)
                    except OSError:
                        break
            if self.process.poll() is None: self.process.kill()
            self.process.wait(timeout=5)
        os.close(self.master);
        os.close(self.slave)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--old-binary', type=Path, required=True)
    args = parser.parse_args()
    old = subprocess.check_output([str(args.old_binary), 'version'], text=True).strip().split()[-1]
    new = subprocess.check_output([str(ROOT / 'cli/build/aura-cli'), 'version'], text=True).strip().split()[-1]
    assert old != new, 'Provide an older source build to exercise a real reload'
    real_cmake = shutil.which('cmake')
    for mode in ('success', 'failure', 'cancel', 'draft'):
        with tempfile.TemporaryDirectory(prefix='aura update test ') as folder:
            base = Path(folder);
            work = base / 'work space';
            work.mkdir()
            install = base / 'private bin';
            install.mkdir()
            (install / 'aura-cli').write_text('original installed file')
            env = {**os.environ, 'TERM': 'xterm-256color', 'AURA_HISTORY': 'off', 'AURA_BIN_DIR': str(install),
                   'AURA_BUILD_DIR': str(ROOT / 'cli/build')}
            env.pop('AURA_CLI_RELOADED', None)
            if mode != 'success':
                fake = base / 'tools';
                fake.mkdir()
                script = fake / 'cmake'
                if mode == 'failure':
                    script.write_text('#!/bin/sh\necho EXPECTED_BUILD_FAILURE\nexit 37\n')
                elif mode == 'cancel':
                    script.write_text('#!/bin/sh\necho UPDATE_TEST_WAIT\nsleep 30\n')
                else:
                    script.write_text(
                        f'#!/bin/sh\nif [ "$1" = "-S" ]; then echo UPDATE_TEST_PAUSE; sleep 2; fi\nexec "{real_cmake}" "$@"\n')
                script.chmod(0o755)
                env['PATH'] = str(fake) + os.pathsep + env['PATH']
            console = Console(args.old_binary, work, env)
            try:
                console.wait('Update available')
                pid = console.process.pid
                console.output = b''
                console.send(b'/update\r')
                if mode == 'failure':
                    console.wait('Your current session is still available')
                    assert (install / 'aura-cli').read_text() == 'original installed file'
                    console.output = b'';
                    console.send(b'/version\r');
                    console.wait(old)
                elif mode == 'cancel':
                    console.wait('UPDATE_TEST_WAIT');
                    console.send(b'\x03')
                    console.wait('Your current session is still available')
                    assert (install / 'aura-cli').read_text() == 'original installed file'
                else:
                    if mode == 'draft':
                        console.wait('UPDATE_TEST_PAUSE');
                        console.send(b'!pwd')
                        console.wait('Finish pending input to reload')
                        assert 'CLI updated' not in ANSI.sub(b'', console.output).decode(errors='replace')
                        console.send(b'\r')
                        console.wait(str(work))
                    console.wait('CLI updated', timeout=90)
                    console.send(b'/version\r');
                    console.wait(new)
                    assert console.process.pid == pid and console.process.poll() is None
                    assert (install / 'aura-cli').is_file()
                    console.output = b'';
                    console.send(b'!pwd\r');
                    console.wait(str(work))
                    console.output = b'';
                    console.send(b'/update\r');
                    console.wait('CLI is up to date')
                console.finish()
                print(f'{mode}: passed', flush=True)
            finally:
                console.close()


if __name__ == '__main__': main()
