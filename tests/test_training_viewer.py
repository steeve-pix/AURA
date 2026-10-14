"""Visual toggles must leave training state and sampled actions unchanged."""
import contextlib
import io
from pathlib import Path
import socket
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "build-release")]

import torch
from cli.training_viewer import TrainingViewer
from cli.nn_session import TrainingEnvironment, TrainingSession
from python.aura_policy import AuraPolicy
from python.train_nn import TrainingConfig, train_batch


class FakeViewer:
    instances = []

    def __init__(self, simulation):
        self.simulation = simulation
        self.open = True
        self.draws = 0
        self.shows = 0
        self.hides = 0
        self.instances.append(self)

    def set_title(self, title):
        pass

    def show(self):
        self.open = True
        self.shows += 1

    def hide(self):
        self.hides += 1

    def is_open(self):
        return self.open

    def render(self, simulation):
        assert simulation is self.simulation
        self.draws += 1


class ViewerTests(unittest.TestCase):
    def test_lazy_toggle_close_reopen_and_draw_limit(self):
        events = []
        simulation = object()
        with patch("aura_python.AuraViewer", FakeViewer), patch("cli.training_viewer.time.monotonic", return_value=1.):
            view = TrainingViewer(simulation, lambda *event: events.append(event))
            view.service()
            count = len(FakeViewer.instances)
            view.toggle()
            renderer = view.viewer
            self.assertEqual(len(FakeViewer.instances), count + 1)
            self.assertEqual(renderer.draws, 1)
            for _ in range(10):
                view.service()
            self.assertEqual(renderer.draws, 1)
            view.toggle()
            self.assertFalse(view.active)
            view.toggle()
            self.assertIs(view.viewer, renderer)
            self.assertEqual(renderer.shows, 2)
            renderer.open = False  # V/Esc/window-close all use this path.
            view.service()
            self.assertFalse(view.active)
            self.assertEqual(events[-1], ("viewer", 0))
            view.toggle()
            self.assertTrue(view.active)
            view.close()
            self.assertIsNone(view.viewer)

    def test_viewer_failure_keeps_headless_mode_available(self):
        events = []
        with patch("aura_python.AuraViewer", side_effect=RuntimeError("No display")):
            view = TrainingViewer(object(), lambda *event: events.append(event))
            view.toggle()
            self.assertFalse(view.active)
            self.assertEqual(events[-1], ("viewer_error", "No display"))
            view.service()

    def test_visual_training_matches_headless_and_services_pause(self):
        from test_nn_configuration import ShortEnvironment

        def create(config):
            torch.manual_seed(config.seed)
            env = ShortEnvironment()
            env.simulation = object()
            policy = AuraPolicy(env.observation_size, hidden_sizes=(7,))
            return env, policy, torch.optim.Adam(policy.parameters(), lr=config.learning_rate)

        config = TrainingConfig(batch_size=3)
        env, policy, optimizer = create(config)
        expected = train_batch(env, policy, optimizer, config.batch_size)
        worker, controller = socket.socketpair()
        try:
            with worker.makefile("r") as stdin, patch("sys.stdin", stdin), patch("cli.nn_session.create_training", create), patch("aura_python.AuraViewer", FakeViewer), contextlib.redirect_stdout(io.StringIO()):
                session = TrainingSession(events=True, interactive=True, config=config)
                session.started()
                controller.sendall(b"visual\n")
                actual = train_batch(TrainingEnvironment(session.env, session), session.policy, session.optimizer, config.batch_size)
                self.assertTrue(session.viewer.active)
                self.assertEqual(
                    {key: value for key, value in actual.items() if key != "states_by_episode"},
                    {key: value for key, value in expected.items() if key != "states_by_episode"},
                )
                for actual_episode, expected_episode in zip(
                    actual["states_by_episode"], expected["states_by_episode"]
                ):
                    for actual_state, expected_state in zip(actual_episode, expected_episode):
                        torch.testing.assert_close(actual_state, expected_state)
                self.assertEqual(session.env.actions, env.actions)
                for actual_weight, expected_weight in zip(session.policy.parameters(), policy.parameters()):
                    self.assertTrue(torch.equal(actual_weight, expected_weight))
                controller.sendall(b"visual\n")
                session.service()
                self.assertFalse(session.viewer.active)
                session.paused = True
                with patch.object(session.viewer, "service", side_effect=lambda: setattr(session, "paused", False)) as service:
                    session.service()
                    service.assert_called_once()
                session.viewer.close()
        finally:
            worker.close()
            controller.close()


if __name__ == "__main__":
    unittest.main()
