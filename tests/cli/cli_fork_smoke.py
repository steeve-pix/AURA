"""Exercise Fork actor through production input handlers; no physical training."""
import tempfile
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

from tests.cli.cli_shortcuts_smoke import Console
from tests.learning.test_reward_repair import RewardRepairTests

with tempfile.TemporaryDirectory() as directory:
    source, _ = RewardRepairTests().source(directory)
    original = source.read_bytes()
    console = Console('setup', directory)
    try:
        console.wait('CHOOSE HOW TO TRAIN')
        console.send(b'5\r')
        console.wait('CHECKPOINT LIBRARY')
        console.send(b'\r')
        console.wait('NEW MODEL')
        console.send(b'\x15fixed_balance\r')
        console.wait('TRAINING RUN')
        console.send(b'\x15' + b'32\t\x15' + b'3\r')
        console.wait('DRIVER_RETURN BACK')
        assert source.read_bytes() == original
        assert list(Path(directory).glob('*.pt')) == [source]
    finally:
        console.close()
print('Fork selection, new name and settings passed; no training or checkpoint changes.')
