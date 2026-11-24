"""Exercise production keyboard handlers with no physics, training or checkpoint writes."""
import fcntl
import os
import pty
import re
import select
import struct
import subprocess
import tempfile
import termios
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Console:
    def __init__(self, *args):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 160, 0, 0))
        self.process = subprocess.Popen([str(ROOT / 'cli/build/cli_shortcut_driver'), *args], cwd=ROOT,
                                        stdin=slave, stdout=slave, stderr=slave,
                                        env={**os.environ, 'TERM': 'xterm-256color', 'AURA_HISTORY': 'off'})
        os.close(slave)
        self.output = bytearray()

    def wait(self, text):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], .05)[0]:
                try:
                    self.output.extend(os.read(self.master, 65536))
                except OSError:
                    pass
            visible = re.sub(rb'\x1b(?:\[[0-9;?]*[ -/]*[@-~]|\([A-Z0-9])', b'', self.output).replace(b'\r\n', b'\n')
            if text.encode() in visible: return
        raise AssertionError((text, self.output[-2500:]))

    def send(self, keys):
        time.sleep(.12)
        self.output.clear()
        os.write(self.master, keys)

    def close(self):
        if self.process.poll() is None: self.process.kill(); self.process.wait()
        os.close(self.master)


def main():
    # Real curses child must not escape the parent's alternate screen lifetime.
    c = Console('library-screen')
    try:
        c.wait('CHILD SCREEN')
        assert c.output.count(b'\x1b[?1049h') == 1
        assert c.output.count(b'\x1b[?1049l') == 0
        c.send(b'q')
        c.wait('Center')
        # Opening and returning never reveal the shell or switch screen buffers.
        assert b'\x1b[?1049l' not in c.output and b'\x1b[?1049h' not in c.output
        c.send(b'\x04')
        c.wait('DRIVER_RETURN')
        assert b'\x1b[?1049l' in c.output
        c.process.wait(timeout=5)
    finally:
        c.close()
    c = Console('screen-reuse')
    try:
        c.wait('Center')
        assert c.output.count(b'\x1b[?1049h') == 1
        assert b'\x1b[?1049l' not in c.output
        c.send(b'\x04');c.wait('DRIVER_RETURN')
        assert c.output.count(b'\x1b[?1049l') == 1
    finally:
        c.close()
    # These are the terminal sequences supplied by the documented keys/profile.
    # Testing the handler avoids altering native clipboard or microphone state.
    for keys, expected in [
        (b'abc\x1b[DZ\r', 'abZc'),
        (b'abc\x1bODZ\r', 'abZc'),
        (b'abc\x01Z\x05!\r', 'Zabc!'),
        (b'abc\x02\x02\x06Z\r', 'abZc'),
        (b'abc\x15\x19\r', 'abc'),
        (b'abc\x01\x0b\x19\r', 'abc'),
        (b'one two\x17Z\r', 'one Z'),
        (b'abc\x1b[H\x1b[3~\r', 'bc'),
        (b'one two\x1bbZ\r', 'one Ztwo'),
        (b'abc\x1b[1;2DZ\r', 'abZ'),
        (b'abc\x1b[97;9uZ\r', 'Z'),
        (b'abc\x1b[127;9uZ\r', 'Z'),
        (b'one two\x1b[127;3uZ\r', 'one Z'),
        (b'one two\x01\x1b[3;3~Z\r', 'Z two'),
        (b'abc\x1aZ\r', 'Z'),
        (b'abc\x1a\x1bZ\r', 'abc'),
        (b'a\x0ab\r', 'a\nb'),
        (b'a\x1b[13;2ub\r', 'a\nb'),
        (b'\x1b[200~pasted\x1b[201~\r', 'pasted'),
        (b'abc\x03', '<EMPTY>'),
        (b'\x04', '<EXIT>'),
    ]:
        c = Console('prompt')
        try:
            c.wait('SIMULATION WORKSPACE')
            c.send(keys)
            c.wait('DRIVER_INPUT ' + expected)
        finally:
            c.close()
    with tempfile.TemporaryDirectory(prefix='aura-shortcuts-') as folder:
        fallback_path = Path(folder) / 'fallback.log'
        fallback = subprocess.run([str(ROOT / 'cli/build/cli_shortcut_driver'), 'control', str(fallback_path)],
                                  input='m\n1 nan\n1 -.5 .2 -.3\nj\n1 0.2 0 0\nq\n', text=True, capture_output=True,
                                  check=True)
        assert 'Enter finite angles only' in fallback.stdout
        fallback_rows = [list(map(float, row.split())) for row in fallback_path.read_text().splitlines()]
        assert len(fallback_rows) == 2
        assert fallback_rows[0][9] == -.5 and fallback_rows[0][10] == -.5
        assert fallback_rows[0][15] == .2 and fallback_rows[0][16] == -.2
        assert fallback_rows[1][0] == .2 and fallback_rows[1][9] == fallback_rows[0][9]
        movement_path = Path(folder) / 'movements.log'
        c = Console('control', str(movement_path))

        def movement_saved(count):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                # Keep consuming rendered frames while waiting for the save;
                # otherwise a full PTY buffer can block the input handler.
                if select.select([c.master], [], [], 0)[0]:
                    c.output.extend(os.read(c.master, 65536))
                if movement_path.exists():
                    rows = movement_path.read_text().splitlines()
                    if len(rows) >= count: return list(map(float, rows[-1].split()))
                time.sleep(.02)
            raise AssertionError('Movement callback not called')

        try:
            c.wait('JOINT VECTORS');
            c.send(b'm');
            c.wait('MOVEMENT VECTORS');
            c.wait('Both arms')
            c.send(b'\x1b[C');
            values = movement_saved(1)
            assert values[9] == .05 and values[10] == .05
            c.send(b'\r');
            c.wait('1 value: bend')
            c.send(b'-.5 .2 -.3\r');
            values = movement_saved(2)
            assert values[9] == -.5 and values[10] == -.5
            assert values[15] == .2 and values[16] == -.2 and values[17] == -.3 and values[18] == .3
            c.send(b'2\t\x1b[C');
            values = movement_saved(3)
            assert values[15] == .25 and values[16] == -.2
            c.wait('Mixed')
            c.send(b'j');
            c.wait('JOINT VECTORS');
            c.wait('SELECTED / Left shoulder')
            c.send(b'm');
            c.wait('MOVEMENT VECTORS')
            c.send(b'\x1b[200~.4\x1b[201~');
            c.wait('.4_')
            c.send(b'\x1b');
            c.wait('[Tab] axis')
            assert len(movement_path.read_text().splitlines()) == 3
            c.send(b'7e');
            c.wait('1 value: turn')
            c.send(b'.1\r');
            values = movement_saved(4)
            assert values[23] == .1 and values[6] == 0
            c.send(b'j');
            c.wait('SELECTED / Waist')
            c.send(b'\x1b[C');
            values = movement_saved(5)
            assert abs(values[23] - .15) < 1e-8 and values[6] == 0
            c.send(b'r');
            assert all(value == 0 for value in movement_saved(6))
            c.send(b'q');
            c.wait('DRIVER_RETURN BACK')
        finally:
            c.close()
        vector_path = Path(folder) / 'vectors.log'
        c = Console('control', str(vector_path))

        def vector_saved(count):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if select.select([c.master], [], [], 0)[0]:
                    c.output.extend(os.read(c.master, 65536))
                if vector_path.exists():
                    rows = vector_path.read_text().splitlines()
                    if len(rows) >= count: return list(map(float, rows[-1].split()))
                time.sleep(.02)
            raise AssertionError('Vector callback not called')

        try:
            c.wait('JOINT VECTORS');
            c.wait('Left hip')
            c.send(b'\t\x1b[C');
            values = vector_saved(1)
            assert values[19] == .05 and values[0] == 0 and values[21] == 0
            c.send(b'\r');
            c.wait('1 value:')
            c.send(b'(-.3,.2,.1)\r');
            values = vector_saved(2)
            assert values[0] == -.3 and values[19] == .2 and values[21] == .1
            c.send(b'3\r');
            c.wait('1 value:')
            c.send(b'.2,1,0\r');
            c.wait('Locked axes must be zero')
            assert len(vector_path.read_text().splitlines()) == 2
            c.send(b'\r9,0,0\r');
            values = vector_saved(3)
            assert values[2] == 2.2 and values[0] == -.3 and values[19] == .2
            c.send(b'\t\x1b[D');
            values = vector_saved(4)
            assert values[2] == 2.15  # Tab skips both locked knee axes.
            c.send(b'\r.4,0,0');
            c.wait('.4,0,0_')
            c.send(b'\x1b');
            c.wait('[Tab] axis')
            assert len(vector_path.read_text().splitlines()) == 4
            c.send(b'1\t\r');
            c.wait('1 value: turn')
            c.send(b'.35\r');
            values = vector_saved(5)
            assert values[19] == .35 and values[0] == -.3 and values[21] == .1
            c.send(b'5\r');
            c.wait('2 values: bend, tilt')
            c.send(b'.15 -.1\r');
            values = vector_saved(6)
            assert values[4] == .15 and values[31] == -.1
            c.send(b'\t\t\r');
            c.wait('1 value: tilt')
            c.send(b'.05\r');
            values = vector_saved(7)
            assert values[4] == .15 and values[31] == .05
            c.send(b'3\r');
            c.wait('1 value: bend')
            c.send(b'.25\r');
            values = vector_saved(8)
            assert values[2] == .25 and values[4] == .15
            c.send(b'q');
            c.wait('DRIVER_RETURN BACK')
        finally:
            c.close()
        path = Path(folder) / 'controls.log'
        c = Console('control', str(path))

        def saved(count, index, value):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if select.select([c.master], [], [], 0)[0]:
                    c.output.extend(os.read(c.master, 65536))
                if path.exists():
                    rows = path.read_text().splitlines()
                    if len(rows) >= count:
                        assert abs(float(rows[-1].split()[index]) - value) < 1e-8, rows[-1]
                        return
                time.sleep(.02)
            raise AssertionError('Control callback not called')

        try:
            c.wait('JOINT VECTORS')
            c.wait('[1-9] joint')
            c.send(b'\x1b[A');
            c.wait('Right wrist')  # Wrap to the last joint.
            c.send(b'\x1b[C');
            saved(1, 14, .05)
            c.send(b'\x1bOD');
            saved(2, 14, 0)
            c.send(b'\x1bOB');
            c.wait('Left hip')
            c.send(b's');
            c.wait('Arrow step: 0.20')
            c.send(b'\x1bOC');
            saved(3, 0, .2)
            c.send(b'9');
            c.wait('Head')
            c.send(b'e');
            c.wait('[Ctrl+U] clear')
            c.send(b'.3\x15-.125\r');
            saved(4, 8, -.125)
            c.send(b'e');
            c.wait('[Ctrl+U] clear')
            c.send(b'.4');
            c.wait('target: .4_')
            c.send(b'\x1b');
            c.wait('[Tab] axis')
            assert len(path.read_text().splitlines()) == 4
            c.send(b'r');
            saved(5, 8, 0)
            assert all(float(v) == 0 for v in path.read_text().splitlines()[-1].split())
            c.send(b'q');
            c.wait('DRIVER_RETURN BACK')
        finally:
            c.close()
        c = Console('control', str(path))
        try:
            c.wait('JOINT VECTORS');
            c.send(b'\x1b');
            c.wait('DRIVER_RETURN HOME')
        finally:
            c.close()
        c = Console('training')
        try:
            c.wait('READY');
            c.wait('[N] next')
            c.send(b'p');
            c.wait('PAUSED');
            c.wait('[P] resume')
            c.send(b'p');
            c.wait('READY');
            c.wait('[N] next')
            c.send(b'v');
            c.wait('[V] headless')
            c.send(b'v');
            c.wait('[V] visual')
            c.send(b'g');
            c.wait('EVALUATION SURVIVAL')
            c.send(b'g');
            c.wait('[G] balance chart')
            c.send(b'g');
            c.wait('EVALUATION BALANCED TIME')
            c.send(b't');
            c.wait('[T] collapse targets')
            c.send(b't');
            c.wait('[T] inspect joint targets')
            c.send(b'2');
            c.wait('PARAMETERS')
            c.send(b'\x1bOC');
            c.wait('Inspection received: inspect 0 1')
            c.send(b'\x1bOD');
            c.wait('Inspection received: inspect 0 0')
            c.send(b'\x1bOB');
            c.wait('Inspection received: inspect 1 0')
            c.send(b'\x1bOA');
            c.wait('Inspection received: inspect 0 0')
            c.send(b'f\x1b[6~');
            c.wait('collapse math')
            c.send(b'3');
            c.wait('REWARD QUALITY')
            c.send(b'r');
            c.wait('FORWARD PASS')
            c.send(b'r');
            c.wait('REWARD QUALITY')
            c.send(b'1n');
            c.wait('TRAINING');
            c.wait('[P] pause')
            c.send(b'q');
            c.wait('LEAVE TRAINING?')
            assert c.process.poll() is None
            c.send(b'n');
            c.wait('[P] pause')
            c.send(b'q');
            c.wait('CONFIRM')
            c.send(b'q');
            c.wait('DRIVER_RETURN BACK')
        finally:
            c.close()
        c = Console('training')
        try:
            c.wait('READY');
            c.send(b'\x1b');
            c.wait('LEAVE TRAINING?')
            c.send(b'\x1b');
            c.wait('DRIVER_RETURN HOME')
        finally:
            c.close()
        c = Console('training')
        try:
            c.wait('READY')
            c.send(b'\x1b\x1b')
            c.wait('DRIVER_RETURN HOME')
        finally:
            c.close()
        for mode, title in [('evaluation', 'EVALUATING'), ('critic', 'TRAINING EPISODES')]:
            for key, destination in [(b'q', 'BACK'), (b'\x1b', 'HOME')]:
                c = Console(mode)
                try:
                    c.wait(title);
                    c.send(key);
                    if mode == 'critic':
                        c.wait('LEAVE TRAINING?')
                        c.send(key)
                    c.wait('DRIVER_RETURN ' + destination)
                finally:
                    c.close()
        c = Console('setup', folder)
        try:
            c.wait('CHOOSE HOW TO TRAIN')
            c.send(b'4\r');
            c.wait('FROZEN ACTOR SOURCE')
            c.send(b'q');
            c.wait('CHOOSE HOW TO TRAIN')
            c.send(b'2\r');
            c.wait('NEW MODEL')
            c.send(b'\x15aura_best\r');
            c.wait('_best is reserved')
            assert b'DRIVER_RETURN' not in c.output
            c.send(b'\x15q');
            c.wait('q')
            c.send(b'\x1b');
            c.wait('CHOOSE HOW TO TRAIN')
            c.send(b'\x1b');
            c.wait('DRIVER_RETURN HOME')
        finally:
            c.close()
        best_only = Path(folder) / 'best-only'
        best_only.mkdir()
        protected = best_only / 'balance_best.pt'
        protected.write_bytes(b'read-only fixture')
        c = Console('setup', str(best_only))
        try:
            c.wait('latest checkpoint required')
            c.send(b'3\x1b[B\r');
            c.wait('FROZEN ACTOR SOURCE')
            c.send(b'q');
            c.wait('CHOOSE HOW TO TRAIN')
            c.send(b'\x1b');
            c.wait('DRIVER_RETURN HOME')
            assert protected.read_bytes() == b'read-only fixture'
        finally:
            c.close()
    print('Control, training dashboard, worker delivery, setup and Q/Esc keyboard checks passed; no models touched.')


if __name__ == '__main__': main()
