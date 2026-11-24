"""Let curses borrow AURA's screen without switching back to the shell."""
import curses
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import sys


def responsive_input(callback,screen):
    """Prevent Ctrl+S flow-control freezes, restoring the terminal on exit."""
    if hasattr(curses,'set_escdelay'):curses.set_escdelay(50)
    if os.name!='posix':return callback(screen)
    import termios
    descriptor=sys.stdin.fileno()
    saved=termios.tcgetattr(descriptor)
    active=saved.copy();active[0]&=~(termios.IXON|termios.IXOFF)
    termios.tcsetattr(descriptor,termios.TCSANOW,active)
    try:return callback(screen)
    finally:termios.tcsetattr(descriptor,termios.TCSANOW,saved)


def run_screen(callback):
    def run(screen):return responsive_input(callback,screen)
    if os.environ.get('AURA_CLI_BORROW_SCREEN') != '1':
        return curses.wrapper(run)
    # Keep all original colors, addressing and key sequences. Only screen-buffer
    # entry/exit belong to the parent CLI. No global terminal profile is changed.
    term=os.environ.get('TERM','')
    compiler=shutil.which('tic')
    if not compiler or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.+-]*',term):
        raise RuntimeError('Cannot open embedded interface: system terminfo compiler or TERM is unavailable')
    with tempfile.TemporaryDirectory(prefix='aura-terminal-') as folder:
        root=Path(folder);source=root/'screen.ti'
        source.write_text(f'aura-borrowed|AURA embedded screen,\n smcup@, rmcup@, use={term},\n')
        result=subprocess.run([compiler,'-o',str(root),str(source)],capture_output=True,text=True)
        if result.returncode:
            raise RuntimeError('Cannot prepare embedded terminal: '+result.stderr.strip())
        previous={key:os.environ.get(key) for key in ('TERM','TERMINFO')}
        os.environ.update(TERM='aura-borrowed',TERMINFO=str(root))
        try:
            return curses.wrapper(run)
        finally:
            for key,value in previous.items():
                if value is None:os.environ.pop(key,None)
                else:os.environ[key]=value
