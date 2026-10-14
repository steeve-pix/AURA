"""Check home feedback against real worker lifecycles without training a model."""
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


def main():
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 140, 0, 0))
    process = subprocess.Popen([str(ROOT / 'cli/build/aura-cli')], cwd=ROOT,
                               stdin=slave, stdout=slave, stderr=slave,
                               env={**os.environ, 'TERM': 'xterm-256color', 'AURA_HISTORY': 'off'})
    os.close(slave)
    output = bytearray()

    def wait_for(text, seconds=10):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if select.select([master], [], [], .05)[0]:
                output.extend(os.read(master, 65536))
            if text.encode() in output:
                return
        raise AssertionError(f'Missing {text!r}: {output[-4000:]!r}')

    def send(text):
        output.clear()
        os.write(master, text.encode())

    try:
        wait_for('Critic: independent')
        wait_for('Idle / no experiment running')
        send('!sleep 1\r')
        wait_for('Running / sleep 1')
        wait_for('Finished / Shell command')
        send('!exit 7\r')
        wait_for('Failed / Shell command')
        wait_for('attention')
        send('!sleep 5\r')
        wait_for('Running / sleep 5')
        send('\x03')
        wait_for('Stopped / Shell command')
        send('/exit\r')
        deadline=time.monotonic()+5
        while process.poll() is None and time.monotonic()<deadline:
            if select.select([master], [], [], .05)[0]:
                try:
                    os.read(master, 65536)
                except OSError:
                    break
        assert process.wait(timeout=3)==0
        print('Home summary: ready, running, finished, failed and stopped states passed.')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)


if __name__ == '__main__':
    main()
