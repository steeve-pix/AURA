"""Keep the transcript anchored while typing, completing and pasting a draft."""
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import struct
import subprocess
import termios
import time

ROOT = Path(__file__).resolve().parents[1]
CONTROL = re.compile(r'\x1b(?:\[[0-?]*[ -/]*[@-~]|\([A-Za-z0-9])')


def screen(data):
    rows = [[' ']*120 for _ in range(30)]
    row = col = 0
    def text(value):
        nonlocal row, col
        for ch in value:
            if ch == '\r': col = 0
            elif ch == '\n': row = min(29, row+1)
            elif ch == '\b': col = max(0, col-1)
            elif ch >= ' ':
                if col < 120: rows[row][col] = ch
                col += 1
    source = data.decode('utf-8', errors='replace')
    offset = 0
    for match in CONTROL.finditer(source):
        text(source[offset:match.start()])
        code = match.group()
        if code.endswith('H') and code.startswith('\x1b['):
            values = code[2:-1].split(';')
            row = min(29, max(0, int(values[0] or 1)-1))
            col = min(119, max(0, int(values[1] or 1)-1)) if len(values)>1 else 0
        elif code in ('\x1b[K', '\x1b[0K'): rows[row][min(col,120):] = [' ']*max(0,120-col)
        elif code == '\x1b[2J': rows = [[' ']*120 for _ in range(30)]
        offset = match.end()
    text(source[offset:])
    return [''.join(line) for line in rows]


def main():
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH',30,120,0,0))
    process = subprocess.Popen(['cli/build/aura-cli'],cwd=ROOT,stdin=slave,stdout=slave,stderr=slave,
                               env=dict(os.environ,TERM='xterm-256color'))
    os.close(slave)
    output = bytearray()
    def wait(predicate):
        deadline = time.monotonic()+15
        while time.monotonic()<deadline:
            if select.select([master],[],[],.05)[0]:
                try: output.extend(os.read(master,65536))
                except OSError: pass
            visible = screen(output)
            if predicate(visible): return visible
        raise AssertionError('\n'.join(screen(output)))
    def send(value): os.write(master,value)
    def markers(rows): return [line.strip() for line in rows[:20] if re.search(r'SCROLL_ROW_\d{3}',line)]
    try:
        wait(lambda rows: any('SIMULATION WORKSPACE' in line for line in rows))
        send(b'''!python3 -c 'for i in range(120): print("SCROLL_ROW_%03d" % i)'\r''')
        wait(lambda rows: any('Completed' in line for line in rows))
        send(b'\x0f')  # Expand output and reveal its beginning.
        visible = wait(lambda rows: any('SCROLL_ROW_000' in line for line in rows))
        first = markers(visible)[0]
        send(b'draft')
        wait(lambda rows: any('draft' in line for line in rows[20:]) and markers(rows) and markers(rows)[0]==first)
        send(b'\x15/')
        wait(lambda rows: any('Search available commands' in line for line in rows) and markers(rows) and markers(rows)[0]==first)
        send(b'\x1b')
        wait(lambda rows: any('SCROLL_ROW_119' in line for line in rows) and not any('Search available commands' in line for line in rows))
        send(b'\x1b[5~'*20)
        wait(lambda rows: markers(rows) and markers(rows)[0]==first)
        send(b'\x15\x1b[200~draft\nsecond\x1b[201~')
        wait(lambda rows: any('second' in line for line in rows[20:]) and markers(rows) and markers(rows)[0]==first)
        send(b'\x15\x10')  # Recall history without changing the output position.
        wait(lambda rows: any('range(120)' in line for line in rows[20:]) and markers(rows) and markers(rows)[0]==first)
        send(b'\x1b')
        wait(lambda rows: any('SCROLL_ROW_119' in line for line in rows) and any('range(120)' in line for line in rows[20:]))
        send(b'\x1b[5~'*2)
        wait(lambda rows: markers(rows) and not any('SCROLL_ROW_119' in line for line in rows))
        send(b'\x1b[6~'*20)
        wait(lambda rows: any('SCROLL_ROW_119' in line for line in rows))
        send(b'\x15/exit\r')
        deadline=time.monotonic()+8
        while process.poll() is None and time.monotonic()<deadline:
            if select.select([master],[],[],.05)[0]:
                try: output.extend(os.read(master,65536))
                except OSError: pass
        assert process.poll()==0, '\n'.join(screen(output))
        print('Transcript stays anchored during typing, suggestions, multiline paste and history recall; Esc returns to latest without losing the draft, and Page Down still works.')
    finally:
        if process.poll() is None: process.kill(); process.wait()
        os.close(master)


if __name__ == '__main__': main()
