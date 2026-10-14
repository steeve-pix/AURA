"""Plain arrows cycle file-picker filters before the draft editor handles them."""
import fcntl
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import termios
import time

ROOT=Path(__file__).resolve().parents[1]


def main():
    master,slave=pty.openpty()
    fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',30,120,0,0))
    process=subprocess.Popen([str(ROOT/'cli/build/aura-cli')],cwd=ROOT,stdin=slave,stdout=slave,stderr=slave,
                             env={**os.environ,'TERM':'xterm-256color','AURA_HISTORY':'off'})
    os.close(slave)
    output=bytearray()
    def wait(text):
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            if select.select([master],[],[],.05)[0]:
                output.extend(os.read(master,65536))
            if text in output: return
        raise AssertionError((text,output[-1500:]))
    def send(keys):
        time.sleep(.12)
        output.clear()
        os.write(master,keys)
    def tab(name): wait(b'\x1b[7m '+name.encode()+b' ')
    try:
        wait(b'SIMULATION WORKSPACE')
        send(b'@')
        tab('All')
        send(b'\x1b[C'); tab('Files'); wait(b'CMakeLists.txt')
        send(b'\x1b[C'); tab('Directories'); wait(b'Browse directory')
        send(b'\x1b[C'); tab('All')
        send(b'\x1b[D'); tab('Directories')
        send(b'\x1bOD'); tab('Files')
        send(b'\x1bOC'); tab('Directories')
        send(b'\x1b')
        time.sleep(.15)
        send(b'/exit\r')
        deadline=time.monotonic()+5
        while process.poll() is None and time.monotonic()<deadline:
            if select.select([master],[],[],.05)[0]:
                try: os.read(master,65536)
                except OSError: break
        assert process.wait(timeout=3)==0
        print('File picker: Left/Right cycle All, Files and Directories; both arrow encodings passed.')
    finally:
        if process.poll() is None: process.kill(); process.wait()
        os.close(master)


if __name__=='__main__': main()
