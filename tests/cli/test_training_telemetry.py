"""Timing must exclude pauses; unavailable GPU readings must not become fake zeros."""
import plistlib
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import torch
from cli.backend.training_telemetry import RunProgress, device_metrics, gpu_utilization


class TimingTests(unittest.TestCase):
    def test_eta_uses_complete_update_cost_and_excludes_pause(self):
        now = [100.]
        progress = RunProgress(clock=lambda: now[0])
        progress.start(3)
        self.assertEqual(progress.snapshot()[3], -1.)
        now[0] += 10
        progress.finish_update()
        self.assertEqual(progress.snapshot()[:4], (1, 3, 10., 20.))
        progress.pause(True)
        now[0] += 300
        self.assertEqual(progress.snapshot()[:4], (1, 3, 10., 20.))
        progress.pause(False)
        now[0] += 5
        self.assertEqual(progress.snapshot()[:4], (1, 3, 15., 15.))
        progress.finish_update()
        now[0] += 5
        progress.finish_update()
        self.assertEqual(progress.snapshot()[:4], (3, 3, 20., 0.))
        now[0] += 100
        self.assertEqual(progress.snapshot()[2], 20.)
        progress.start(2)
        self.assertEqual(progress.snapshot(), (0, 2, 0., -1., 0))

    def test_slow_update_never_has_a_negative_eta_or_false_completion(self):
        now = [0.]
        progress = RunProgress(clock=lambda: now[0])
        progress.start(2)
        now[0] = 10.
        progress.finish_update()
        now[0] = 500.
        self.assertEqual(progress.snapshot()[:2], (1, 2))
        self.assertGreater(progress.snapshot()[3], 0.)


class DeviceTests(unittest.TestCase):
    def test_monitor_samples_without_simulation_steps_and_stops_on_close(self):
        from cli.backend.nn_session import TrainingSession
        session = TrainingSession.__new__(TrainingSession)
        session.device_monitor_stop = threading.Event()
        session.device_monitor_thread = None
        session.policy = session.critic = torch.nn.Linear(2, 1)
        session.gpu_updates = 0
        session.telemetry_phase, session.paused = 'optimizing', False
        sampled = threading.Event()
        session.emit = lambda *event: sampled.set() if event[:2] == ('device_metric', 'phase') else None
        session.set_device_info(True)
        worker = session.device_monitor_thread
        try:
            self.assertTrue(sampled.wait(2.))
        finally:
            session.set_device_info(False)
        self.assertFalse(worker.is_alive())

    def test_apple_usage_is_labeled_system_wide(self):
        result = SimpleNamespace(stdout=plistlib.dumps([
            {'PerformanceStatistics': {'Device Utilization %': 37}}]))
        with patch('cli.backend.training_telemetry.sys.platform', 'darwin'), \
                patch('cli.backend.training_telemetry.subprocess.run', return_value=result) as query:
            self.assertEqual(gpu_utilization(torch.device('mps')), '37% / system-wide, includes other apps')
            self.assertLessEqual(query.call_args.kwargs['timeout'], 1.)

    def test_unavailable_and_invalid_usage_never_report_zero(self):
        with patch('cli.backend.training_telemetry.sys.platform', 'darwin'), \
                patch('cli.backend.training_telemetry.subprocess.run', side_effect=OSError()):
            self.assertIn('Unavailable', gpu_utilization(torch.device('mps')))
        result = SimpleNamespace(stdout=plistlib.dumps([
            {'PerformanceStatistics': {'Device Utilization %': 170}}]))
        with patch('cli.backend.training_telemetry.sys.platform', 'darwin'), \
                patch('cli.backend.training_telemetry.subprocess.run', return_value=result):
            self.assertIn('Unavailable', gpu_utilization(torch.device('mps')))

    def test_cpu_does_not_query_gpu_or_change_parameters(self):
        actor, critic = torch.nn.Linear(2, 2), torch.nn.Linear(2, 1)
        before = actor.weight.detach().clone()
        with patch('cli.backend.training_telemetry.gpu_utilization') as query:
            values = device_metrics(actor, critic)
            query.assert_not_called()
        self.assertEqual(values['actor'], 'cpu')
        self.assertEqual(values['critic'], 'cpu')
        self.assertEqual(values['allocated'], 'Not applicable')
        self.assertTrue(torch.equal(before, actor.weight))

    def test_mps_memory_comes_from_pytorch_allocations(self):
        model = SimpleNamespace(parameters=lambda: iter([SimpleNamespace(device=torch.device('mps:0'))]))
        with patch('cli.backend.training_telemetry.gpu_utilization', return_value='12% / system-wide'), \
                patch('torch.mps.current_allocated_memory', return_value=1024 ** 2), \
                patch('torch.mps.driver_allocated_memory', return_value=4 * 1024 ** 2), \
                patch('torch.mps.recommended_max_memory', return_value=1024 ** 3):
            values = device_metrics(model, model, 2)
        self.assertIn('1.0 MiB', values['allocated'])
        self.assertIn('4.0 MiB', values['reserved'])
        self.assertIn('2 paired GPU updates', values['updates'])


if __name__ == '__main__':
    unittest.main()
