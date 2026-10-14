"""Best promotion preserves measured weights and survives session boundaries."""

from pathlib import Path
import fcntl
import tempfile
import sys
import unittest
from unittest.mock import patch
from types import SimpleNamespace

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
                str(Path(__file__).resolve().parents[1] / "build-release")]
import torch

from python.aura_policy import AuraPolicy
from python.model_checkpoint import promote_best_checkpoint, save_checkpoint


class BestCheckpointTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.latest = Path(self.directory.name) / "balance.pt"
        self.best = self.latest.with_name("balance_best.pt")
        self.policy = AuraPolicy(5, hidden_sizes=(3,))
        self.optimizer = torch.optim.Adam(self.policy.parameters())
        save_checkpoint(self.policy, self.optimizer, 8, self.latest)

    def test_strict_improvement_preserves_exact_state_and_latest(self):
        original = self.latest.read_bytes()
        self.assertEqual(promote_best_checkpoint(self.latest, 280), self.best)
        data = torch.load(self.best, weights_only=True)
        latest_data = torch.load(self.latest, weights_only=True)
        self.assertEqual(data["best_evaluation_steps"], 280)
        self.assertEqual(data["episodes_completed"], 8)
        self.assertEqual(data["optimizer"], latest_data["optimizer"])
        self.assertTrue(torch.equal(data["rng_state"], latest_data["rng_state"]))
        for name, weight in latest_data["model"].items():
            self.assertTrue(torch.equal(weight, data["model"][name]))
        best_bytes = self.best.read_bytes()
        # A later run reads the score from disk, without in-memory tracking.
        with torch.no_grad():
            next(self.policy.parameters()).add_(1)
        save_checkpoint(self.policy, self.optimizer, 16, self.latest)
        for score in (230, 280):
            self.assertIsNone(promote_best_checkpoint(self.latest, score))
            self.assertEqual(self.best.read_bytes(), best_bytes)
        changed_latest = self.latest.read_bytes()
        self.assertNotEqual(changed_latest, original)
        self.assertEqual(promote_best_checkpoint(self.latest, 310), self.best)
        promoted = torch.load(self.best, weights_only=True)
        self.assertEqual(promoted["best_evaluation_steps"], 310)
        self.assertEqual(promoted["episodes_completed"], 16)
        for name, weight in self.policy.state_dict().items():
            self.assertTrue(torch.equal(weight, promoted["model"][name]))
        self.assertEqual(self.latest.read_bytes(), changed_latest)

    def test_restart_requires_explicit_reset_and_names_are_independent(self):
        promote_best_checkpoint(self.latest, 310)
        self.assertIsNone(promote_best_checkpoint(self.latest, 200))
        promote_best_checkpoint(self.latest, 200, reset_best=True)
        self.assertEqual(torch.load(self.best, weights_only=True)["best_evaluation_steps"], 200)
        other = self.latest.with_name("other.pt")
        save_checkpoint(self.policy, self.optimizer, 0, other)
        promote_best_checkpoint(other, 100)
        self.assertEqual(torch.load(self.best, weights_only=True)["best_evaluation_steps"], 200)

    def test_initial_policy_remains_best_until_a_later_evaluation_beats_it(self):
        policy = AuraPolicy(5, hidden_sizes=(3,))
        optimizer = torch.optim.Adam(policy.parameters())
        latest = Path(self.directory.name) / "progression.pt"
        best = latest.with_name("progression_best.pt")
        initial_state = {key: value.clone() for key, value in policy.state_dict().items()}

        save_checkpoint(policy, optimizer, 0, latest)
        self.assertEqual(torch.load(latest, weights_only=True)["training_status"], "untrained")
        self.assertEqual(promote_best_checkpoint(latest, 377), best)
        saved = torch.load(best, weights_only=True)
        self.assertEqual(saved["best_evaluation_steps"], 377)
        self.assertEqual(saved["training_status"], "untrained")
        for key, value in initial_state.items():
            self.assertTrue(torch.equal(saved["model"][key], value))

        for update, score in enumerate((346, 360, 390), start=1):
            for parameter in policy.parameters():
                parameter.grad = torch.ones_like(parameter)
            optimizer.step()
            save_checkpoint(policy, optimizer, update * 8, latest)
            promoted = promote_best_checkpoint(latest, score)
            saved = torch.load(best, weights_only=True)
            if score < 377:
                self.assertIsNone(promoted)
                self.assertEqual(saved["best_evaluation_steps"], 377)
                self.assertEqual(saved["training_status"], "untrained")
                for key, value in initial_state.items():
                    self.assertTrue(torch.equal(saved["model"][key], value))
            else:
                self.assertEqual(promoted, best)
                self.assertEqual(saved["best_evaluation_steps"], 390)
                self.assertEqual(saved["training_status"], "trained")
                self.assertEqual(saved["updates_completed"], 3)
                for key, value in policy.state_dict().items():
                    self.assertTrue(torch.equal(saved["model"][key], value))
    def test_run_identity_survives_resume_after_restart_save(self):
        save_checkpoint(self.policy, self.optimizer, 8, self.latest, training_run_id="original")
        promote_best_checkpoint(self.latest, 310)
        # Restart saves new identity; a later resume must not inherit the old best,
        # even when the process stopped before its first evaluation promotion.
        save_checkpoint(self.policy, self.optimizer, 8, self.latest, training_run_id="restarted")
        promote_best_checkpoint(self.latest, 200)
        self.assertEqual(torch.load(self.best, weights_only=True)["best_evaluation_steps"], 200)
        self.assertIsNone(promote_best_checkpoint(self.latest, 190))

    def test_training_session_promotes_after_evaluation_before_ready(self):
        from cli.nn_session import TrainingSession
        session = TrainingSession.__new__(TrainingSession)
        session.env, session.policy, session.optimizer = object(), self.policy, self.optimizer
        session.config = SimpleNamespace(batch_size=2)
        session.parameters = list(self.policy.named_parameters())
        session.update = 0
        session.best_initialized = False
        session.events, session.interactive = True, False
        session.started = lambda: None
        session.updated = lambda *_: setattr(session, "update", session.update + 1)
        events = []
        session.emit = lambda *event: events.append(event)
        training = SimpleNamespace(episodes_completed=0, run_id="session-test")

        def save_current():
            return save_checkpoint(self.policy, self.optimizer, training.episodes_completed,
                                   self.latest, training_run_id=training.run_id)

        def save(count):
            training.episodes_completed += count
            return save_current()

        training.save_current = save_current
        training.save = save
        session.training = training
        initial_state = {key: value.clone() for key, value in self.policy.state_dict().items()}
        with patch("cli.nn_session.train_batch", return_value={"episode_scores": [1, 2]}), \
                patch("cli.nn_session.evaluate_policy", side_effect=[377, 310, 230]) as evaluate:
            session.train()
            original = self.best.read_bytes()
            initial_best = torch.load(self.best, weights_only=True)
            self.assertEqual(initial_best["best_evaluation_steps"], 377)
            self.assertEqual(initial_best["training_status"], "untrained")
            self.assertEqual(initial_best["episodes_completed"], 0)
            for key, value in initial_state.items():
                self.assertTrue(torch.equal(initial_best["model"][key], value))
            session.train()
            self.assertEqual(self.best.read_bytes(), original)
            evaluate.assert_called_with(session.env, session.policy)
        self.assertIn(("evaluation", 0, 377), events)
        self.assertEqual(events[-3:], [("evaluation", 2, 230), ("best", 377, "balance_best.pt"), ("phase", "ready")])
        self.assertEqual(torch.load(self.latest, weights_only=True)["episodes_completed"], 4)

    def test_atomic_failure_collision_invalid_score_and_active_writer(self):
        for score in (-1, 2.5, float("nan"), True):
            with self.assertRaises(ValueError):
                promote_best_checkpoint(self.latest, score)
        save_checkpoint(self.policy, self.optimizer, 0, self.best)
        unrelated = self.best.read_bytes()
        with self.assertRaisesRegex(ValueError, "unrelated"):
            promote_best_checkpoint(self.latest, 100, reset_best=True)
        self.assertEqual(self.best.read_bytes(), unrelated)
        self.best.unlink()
        promote_best_checkpoint(self.latest, 100)
        original = self.best.read_bytes()
        with patch("python.model_checkpoint.torch.save", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                promote_best_checkpoint(self.latest, 200)
        self.assertEqual(self.best.read_bytes(), original)
        self.assertEqual(list(self.latest.parent.glob("*.tmp")), [])
        with (self.best.parent / f".{self.best.name}.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaisesRegex(ValueError, "in use"):
                promote_best_checkpoint(self.latest, 200)


if __name__ == "__main__":
    unittest.main()
