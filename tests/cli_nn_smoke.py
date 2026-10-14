"""Real headless neural training and CLI PTY integration. No graphical session needed."""

import fcntl
import math
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import sys
import termios
import time

ROOT = Path(__file__).resolve().parents[1]
MODEL_NAME = f"zz_nn_smoke_{os.getpid()}"
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'build-release'))


def check_adapter():
    """CLI observation must preserve scores, sampled actions, gradients and Adam state."""
    import contextlib
    import io
    import torch
    from cli.nn_session import TrainingEnvironment, TrainingSession
    from python.aura_policy import AuraPolicy
    from python.aura_env import AuraEnv
    from python.train_nn import train_batch

    reference_env = AuraEnv()
    observation_size = reference_env.observation_size

    class ShortEnv:
        episode_limit = reference_env.episode_limit
        def __init__(self):
            self.episode = 0
            self.actions = []

        def reset(self):
            self.episode += 1
            self.steps = 0
            return torch.zeros(observation_size), {}

        def step(self, action):
            self.actions.append(action)
            self.steps += 1
            done = self.steps == self.episode + 2
            info = dict(steps=self.steps, pelvis_height=4.0, pelvis_uprightness=1.0,
                        terminated=done, truncated=False, termination_reason='fallen')
            return torch.full((observation_size,), self.steps / 100), 1.0, done, False, info

    torch.manual_seed(42)
    plain_env = ShortEnv()
    plain_policy = AuraPolicy(observation_size)
    plain_optimizer = torch.optim.Adam(plain_policy.parameters(), lr=.001)
    plain_result = train_batch(plain_env, plain_policy, plain_optimizer)

    with contextlib.redirect_stdout(io.StringIO()) as output:
        session = TrainingSession(events=True)
        session.env = ShortEnv()
        session.started()
        before = [parameter.detach().clone() for parameter in session.policy.parameters()]
        result = train_batch(TrainingEnvironment(session.env, session), session.policy, session.optimizer)
        session.updated(result, before)
    assert {key: value for key, value in result.items() if key != "states_by_episode"} == {
        key: value for key, value in plain_result.items() if key != "states_by_episode"
    }
    for actual_episode, expected_episode in zip(result["states_by_episode"], plain_result["states_by_episode"]):
        for actual, expected in zip(actual_episode, expected_episode):
            assert torch.equal(actual, expected)
    assert session.env.actions == plain_env.actions
    for actual, expected in zip(session.policy.parameters(), plain_policy.parameters()):
        assert torch.equal(actual, expected)
        assert torch.equal(actual.grad, expected.grad)
        for key in ('exp_avg', 'exp_avg_sq', 'step'):
            assert torch.equal(session.optimizer.state[actual][key], plain_optimizer.state[expected][key])
    assert output.getvalue().count('AURA_NN\tepisode\t') == 8
    assert 'AURA_NN\tphase\toptimizing' in output.getvalue()
    print('CLI adapter preserves exact actions, results, weights, gradients and Adam state', flush=True)


def check_worker():
    process = subprocess.Popen(
        ['python3', '-m', 'cli.nn_session', '--events', '--interactive', '--name', MODEL_NAME], cwd=ROOT,
        env={**os.environ, 'PYTHONPATH': str(ROOT / 'build-release')},
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    # Piped commands are handled at step boundaries, including the review phase.
    # Request next only after ready, so this exercises actual retained Adam state.
    updates, selected, scores = [], [], []
    try:
        for line in process.stdout:
            parts = line.rstrip('\n').split('\t')
            if parts[:2] == ['AURA_NN', 'episode']:
                scores.append(int(parts[4]))
            if parts[:2] == ['AURA_NN', 'inspect'] and int(parts[5]) > 0:
                selected.append(parts)
            if parts[:2] == ['AURA_NN', 'update']:
                updates.append(parts)
                assert all(math.isfinite(float(value)) for value in parts[3:])
            if parts[:3] == ['AURA_NN', 'phase', 'ready']:
                if len(updates) == 0:
                    # The initial policy is evaluated before the first batch.
                    continue
                elif len(updates) == 1:
                    process.stdin.write('inspect 5 3\nnext\n')
                else:
                    process.stdin.write('quit\n')
                process.stdin.flush()
        assert process.wait(timeout=10) == 0, process.stderr.read()
        assert len(scores) == 16 and len(updates) == 2
        assert [int(row[2]) for row in updates] == [1, 2]
        assert float(updates[0][-1]) > 0
        assert selected and any(row[2:4] == ['5', '3'] for row in selected)
        for row in selected:
            step = int(row[5])
            before, after, gradient, moment, variance = map(float, row[6:])
            delta = -.001 * (moment / (1 - .9 ** step)) / (math.sqrt(variance / (1 - .999 ** step)) + 1e-8)
            assert math.isclose(after - before, delta, abs_tol=2e-8)
        print('Real training: two Adam updates, finite measurements, scalar formula matches float32 update', flush=True)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def check_dashboard():
    master, slave = pty.openpty()
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', 32, 120, 0, 0))
    process = subprocess.Popen(
        [str(ROOT / 'cli/build/aura-cli'), 'train_nn'], cwd=ROOT,
        stdin=slave, stdout=slave, stderr=slave, start_new_session=True,
        env={**os.environ, 'TERM': 'xterm-256color', 'LANG': 'en_US.UTF-8'},
    )
    os.close(slave)
    output = bytearray()

    def read():
        while select.select([master], [], [], 0)[0]:
            try:
                chunk = os.read(master, 65536)
            except OSError:
                break
            if not chunk:
                break
            output.extend(chunk)

    def wait(needle, mark=0, timeout=30):
        if os.getenv("AURA_TEST_TRACE"):
            print(f"Waiting for {needle!r}", flush=True)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            read()
            if needle in output[mark:]:
                return
            if process.poll() is not None:
                break
            time.sleep(.025)
        raise AssertionError(f'Missing {needle!r}: {output[-2500:]!r}')

    def send(keys):
        read()
        mark = len(output)
        os.write(master, keys)
        return mark

    try:
        wait(b'TRAINING SETUP')
        send(b'2\r')
        wait(b'Model name')
        send(b'\x15' + MODEL_NAME.encode() + b'\r')
        wait(b'NEURAL TRAINING LAB')
        wait('↻ TRAINING'.encode())
        mark = send(b'p')
        wait(b'PAUSED', mark)
        paused_start = mark
        time.sleep(.25)
        read()
        mark = len(output)
        time.sleep(.4)
        read()
        assert len(output) == mark, 'Paused workspace repainted unnecessarily'
        assert b'\x1b[?25h' not in output[paused_start:], 'Dashboard showed a text cursor'
        mark = send(b'2')
        wait(b'network.0.weight', mark)
        mark = send(b'\x1b[B\x1b[C')
        wait(b'Element 2 / 64', mark)
        mark = send(b'\x1b[F')
        wait(b'Element 64 / 64', mark)
        # Ignore mouse reports and bracketed pasted commands.
        send(b'\x1b[<64;40;10M\x1b[200~qnp\x1b[201~')
        time.sleep(.2)
        assert process.poll() is None
        mark = send(b'3')
        wait(b'POLICY GRADIENT / REWARD-TO-GO', mark)
        send(b'1p')
        wait(b'READY', timeout=240)
        mark = send(b'2')
        wait(b'ADAM / UPDATE RULE', mark)
        wait(b'Calculated ', mark)
        mark = send(b'f')
        wait(b'collapse math', mark)
        mark = send(b'\x1b[6~')
        wait(b'bias corrected', mark)
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', 12, 45, 0, 0))
        mark = send(b'1')
        wait(b'Enlarge terminal', mark)
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', 32, 120, 0, 0))
        mark = send(b'n')
        wait('↻ TRAINING'.encode(), mark)
        mark = send(b'p')
        wait(b'PAUSED', mark)
        send(b'q')
        deadline = time.monotonic() + 10
        while process.poll() is None and time.monotonic() < deadline:
            read()
            time.sleep(.025)
        assert process.poll() == 0, output[-1000:]
        print('CLI: plots, weights, pause/resume, formulas, next batch, resize, clean exit passed', flush=True)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)


if __name__ == '__main__':
    try:
        check_adapter()
        check_worker()
        check_dashboard()
    finally:
        for artifact in (ROOT / 'models').glob(MODEL_NAME + '*.pt'):
            artifact.unlink()
        for lock in (ROOT / 'models').glob('.' + MODEL_NAME + '*.lock'):
            lock.unlink()
