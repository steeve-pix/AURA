"""Read-only run timing and optional device measurements for the training UI."""
from collections import deque
import math
import plistlib
import subprocess
import sys
import time

import torch


class RunProgress:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.start_time = None
        self.paused_at = None
        self.paused_seconds = 0.
        self.total = 1
        self.completed = self.episodes = 0
        self.last_completion = 0.
        self.durations = deque(maxlen=8)

    def start(self, total):
        self.start_time = self.clock()
        self.total = total
        self.completed = self.episodes = 0
        self.paused_seconds = self.last_completion = 0.
        self.paused_at = self.clock() if self.paused_at is not None else None
        self.durations.clear()

    def pause(self, paused):
        if paused and self.paused_at is None:
            self.paused_at = self.clock()
        elif not paused and self.paused_at is not None:
            self.paused_seconds += self.clock() - self.paused_at
            self.paused_at = None

    def snapshot(self):
        if self.start_time is None:
            return self.completed, self.total, 0., -1., self.episodes
        end = self.paused_at if self.paused_at is not None else self.clock()
        elapsed = max(0., end - self.start_time - self.paused_seconds)
        if self.completed == self.total:
            elapsed = self.last_completion
            eta = 0.
        elif self.durations:
            average = sum(self.durations) / len(self.durations)
            eta = max(1., average * (self.total - self.completed) - (elapsed - self.last_completion))
        else:
            eta = -1.
        return self.completed, self.total, elapsed, eta, self.episodes

    def finish_update(self):
        elapsed = self.snapshot()[2]
        self.durations.append(max(0., elapsed - self.last_completion))
        self.last_completion = elapsed
        self.completed += 1


def gpu_utilization(device):
    """OS-wide reading without elevated permissions or persistent sampling processes."""
    try:
        if device.type == 'mps' and sys.platform == 'darwin':
            result = subprocess.run(['/usr/sbin/ioreg', '-r', '-c', 'IOAccelerator', '-a'],
                                    capture_output=True, timeout=1, check=True)
            values = [entry.get('PerformanceStatistics', {}).get('Device Utilization %')
                      for entry in plistlib.loads(result.stdout)]
            values = [v for v in values if isinstance(v, (int, float)) and math.isfinite(v) and 0 <= v <= 100]
            if values:
                return f'{max(values):.0f}% / system-wide, includes other apps'
        elif device.type == 'cuda':
            result = subprocess.run(['nvidia-smi', '--query-gpu=utilization.gpu',
                                     '--format=csv,noheader,nounits', '-i', str(device.index or 0)],
                                    capture_output=True, text=True, timeout=1, check=True)
            value = float(result.stdout.strip())
            if math.isfinite(value) and 0 <= value <= 100:
                return f'{value:.0f}% / system-wide, includes other apps'
    except (OSError, subprocess.SubprocessError, ValueError, TypeError, plistlib.InvalidFileException):
        pass
    return 'Unavailable / OS does not expose a usable sample'


def device_metrics(actor, critic, gpu_updates=0):
    actor_device = next(actor.parameters()).device
    critic_device = next(critic.parameters()).device
    gpu = actor_device.type in ('mps', 'cuda')
    values = {
        'backend': 'Apple Metal / MPS' if actor_device.type == 'mps' else
                   'NVIDIA CUDA' if actor_device.type == 'cuda' else 'CPU',
        'actor': str(actor_device), 'critic': str(critic_device),
        'physics': 'CPU / C++ rigid-body simulation',
        'neural': 'GPU / inference, gradients and optimizer updates' if gpu else
                  'CPU / inference, gradients and optimizer updates',
        'updates': f'{gpu_updates} paired GPU updates completed in this session',
        'utilization': gpu_utilization(actor_device) if gpu else 'Not sampled / CPU selected',
    }
    def memory(value):
        return f'{value / (1024 ** 2):.1f} MiB'
    try:
        if actor_device.type == 'mps':
            values['allocated'] = memory(torch.mps.current_allocated_memory()) + ' / live PyTorch tensors'
            values['reserved'] = memory(torch.mps.driver_allocated_memory()) + ' / Metal driver incl. cache'
            values['budget'] = memory(torch.mps.recommended_max_memory()) + ' / recommended shared-memory budget'
        elif actor_device.type == 'cuda':
            values['allocated'] = memory(torch.cuda.memory_allocated(actor_device)) + ' / live PyTorch tensors'
            values['reserved'] = memory(torch.cuda.memory_reserved(actor_device)) + ' / PyTorch allocator incl. cache'
            values['budget'] = memory(torch.cuda.get_device_properties(actor_device).total_memory) + ' / device capacity'
        else:
            values.update(allocated='Not applicable', reserved='Not applicable', budget='Not applicable')
    except (RuntimeError, AttributeError):
        values.update(allocated='Unavailable', reserved='Unavailable', budget='Unavailable')
    return values
