"""Exercise production keyboard handlers with no physics, training or checkpoint writes."""
import fcntl
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import tempfile
import termios
import time

ROOT=Path(__file__).resolve().parents[1]


class Console:
    def __init__(self,*args):
        self.master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',40,160,0,0))
        self.process=subprocess.Popen([str(ROOT/'cli/build/cli_shortcut_driver'),*args],cwd=ROOT,
            stdin=slave,stdout=slave,stderr=slave,env={**os.environ,'TERM':'xterm-256color','AURA_HISTORY':'off'})
        os.close(slave)
        self.output=bytearray()
    def wait(self,text):
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            if select.select([self.master],[],[],.05)[0]:
                try: self.output.extend(os.read(self.master,65536))
                except OSError: pass
            if text.encode() in self.output.replace(b'\r\n',b'\n'): return
        raise AssertionError((text,self.output[-2500:]))
    def send(self,keys):
        time.sleep(.12)
        self.output.clear()
        os.write(self.master,keys)
    def close(self):
        if self.process.poll() is None: self.process.kill(); self.process.wait()
        os.close(self.master)


def main():
    # These are the terminal sequences supplied by the documented keys/profile.
    # Testing the handler avoids altering native clipboard or microphone state.
    for keys,expected in [
        (b'abc\x1b[DZ\r','abZc'),
        (b'abc\x1bODZ\r','abZc'),
        (b'abc\x01Z\x05!\r','Zabc!'),
        (b'abc\x02\x02\x06Z\r','abZc'),
        (b'abc\x15\x19\r','abc'),
        (b'abc\x01\x0b\x19\r','abc'),
        (b'one two\x17Z\r','one Z'),
        (b'abc\x1b[H\x1b[3~\r','bc'),
        (b'one two\x1bbZ\r','one Ztwo'),
        (b'abc\x1b[1;2DZ\r','abZ'),
        (b'abc\x1b[97;9uZ\r','Z'),
        (b'abc\x1b[127;9uZ\r','Z'),
        (b'one two\x1b[127;3uZ\r','one Z'),
        (b'one two\x01\x1b[3;3~Z\r','Z two'),
        (b'abc\x1aZ\r','Z'),
        (b'abc\x1a\x1bZ\r','abc'),
        (b'a\x0ab\r','a\nb'),
        (b'a\x1b[13;2ub\r','a\nb'),
        (b'\x1b[200~pasted\x1b[201~\r','pasted'),
        (b'abc\x03','<EMPTY>'),
        (b'\x04','<EXIT>'),
    ]:
        c=Console('prompt')
        try:
            c.wait('SIMULATION WORKSPACE')
            c.send(keys)
            c.wait('DRIVER_INPUT '+expected)
        finally: c.close()
    with tempfile.TemporaryDirectory(prefix='aura-shortcuts-') as folder:
        path=Path(folder)/'controls.log'
        c=Console('control',str(path))
        def saved(count,index,value):
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                if path.exists():
                    rows=path.read_text().splitlines()
                    if len(rows)>=count:
                        assert abs(float(rows[-1].split()[index])-value)<1e-8,rows[-1]
                        return
                time.sleep(.02)
            raise AssertionError('Control callback not called')
        try:
            c.wait('JOINT CONTROL')
            c.wait('1-9 quick select')
            c.send(b'\x1b[A'); c.wait('Right wrist limits')  # Wrap to the last joint.
            c.send(b'\x1b[C'); saved(1,14,.05)
            c.send(b'\x1bOD'); saved(2,14,0)
            c.send(b'\x1bOB'); c.wait('Left hip limits')
            c.send(b's'); c.wait('Arrow step: 0.20')
            c.send(b'\x1bOC'); saved(3,0,.2)
            c.send(b'9'); c.wait('Head limits')
            c.send(b'\r'); c.wait('Ctrl+U clear')
            c.send(b'.3\x15-.125\r'); saved(4,8,-.125)
            c.send(b'\r'); c.wait('Ctrl+U clear')
            c.send(b'.4\x1b'); c.wait('Q back / Esc home')
            assert len(path.read_text().splitlines())==4
            c.send(b'r'); saved(5,8,0)
            assert all(float(v)==0 for v in path.read_text().splitlines()[-1].split())
            c.send(b'q'); c.wait('DRIVER_RETURN BACK')
        finally: c.close()
        c=Console('control',str(path))
        try:
            c.wait('JOINT CONTROL'); c.send(b'\x1b'); c.wait('DRIVER_RETURN HOME')
        finally: c.close()
        c=Console('training')
        try:
            c.wait('READY'); c.wait('N next')
            c.send(b'p'); c.wait('PAUSED'); c.wait('P resume')
            c.send(b'p'); c.wait('READY'); c.wait('N next')
            c.send(b'v'); c.wait('V headless')
            c.send(b'v'); c.wait('V visual')
            c.send(b'g'); c.wait('POLICY LOSS')
            c.send(b'g'); c.wait('EVALUATION SURVIVAL')
            c.send(b't'); c.wait('T collapse targets')
            c.send(b't'); c.wait('T inspect joint targets')
            c.send(b'2'); c.wait('PARAMETERS')
            c.send(b'\x1bOC'); c.wait('Inspection received: inspect 0 1')
            c.send(b'\x1bOD'); c.wait('Inspection received: inspect 0 0')
            c.send(b'\x1bOB'); c.wait('Inspection received: inspect 1 0')
            c.send(b'\x1bOA'); c.wait('Inspection received: inspect 0 0')
            c.send(b'f\x1b[6~'); c.wait('collapse math')
            c.send(b'3'); c.wait('REWARD QUALITY')
            c.send(b'r'); c.wait('FORWARD PASS')
            c.send(b'r'); c.wait('REWARD QUALITY')
            c.send(b'1n'); c.wait('TRAINING'); c.wait('P pause')
            c.send(b'q'); c.wait('DRIVER_RETURN BACK')
        finally: c.close()
        c=Console('training')
        try:
            c.wait('READY'); c.send(b'\x1b'); c.wait('DRIVER_RETURN HOME')
        finally: c.close()
        for mode,title in [('evaluation','EVALUATING'),('critic','TRAINING EPISODES')]:
            for key,destination in [(b'q','BACK'),(b'\x1b','HOME')]:
                c=Console(mode)
                try:
                    c.wait(title); c.send(key); c.wait('DRIVER_RETURN '+destination)
                finally: c.close()
        c=Console('setup',folder)
        try:
            c.wait('CHOOSE HOW TO TRAIN')
            c.send(b'4\r'); c.wait('FROZEN ACTOR SOURCE')
            c.send(b'q'); c.wait('CHOOSE HOW TO TRAIN')
            c.send(b'2\r'); c.wait('NEW MODEL')
            c.send(b'\x15q'); c.wait('q')
            c.send(b'\x1b'); c.wait('CHOOSE HOW TO TRAIN')
            c.send(b'\x1b'); c.wait('DRIVER_RETURN HOME')
        finally: c.close()
    print('Control, training dashboard, worker delivery, setup and Q/Esc keyboard checks passed; no models touched.')


if __name__=='__main__': main()
