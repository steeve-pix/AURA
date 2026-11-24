"""Real run-settings keyboard handler, without physics, optimizers, or checkpoints."""
import fcntl
import os
import pty
import select
import struct
import subprocess
import termios
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(mode, invalid, value, expected):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 120, 0, 0))
    process = subprocess.Popen(['cli/build/cli_shortcut_driver', 'schedule', mode], cwd=ROOT,
                               stdin=slave, stdout=slave, stderr=slave, env={**os.environ, 'TERM': 'xterm-256color'})
    os.close(slave)
    output = bytearray()
    def wait(text):
        deadline = time.monotonic() + 10
        while text.encode() not in output:
            if select.select([master], [], [], .05)[0]:
                try:
                    output.extend(os.read(master, 65536))
                except OSError:
                    pass
            if time.monotonic() > deadline:
                raise AssertionError((text, output[-2500:]))
    try:
        wait('TRAINING RUN')
        if invalid:
            os.write(master, b'\x15' + invalid.encode() + b'\r')
            wait('episodes must be divisible by agents')
            assert process.poll() is None
        os.write(master, b'\x15' + value.encode() + b'\r')
        wait('DRIVER_SCHEDULE ' + expected)
        process.wait(timeout=5)
        assert process.returncode == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)


if __name__ == '__main__':
    run('new', '7 2 4', '16 3 4', '16 3 4')
    run('resume', None, '12 2', '12 2 0')
    print('CLI run settings: valid new/resume schedules and invalid arena batch rejection passed.')
