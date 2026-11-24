"""Read-only tools and history persistence must never update model parameters."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import torch
from cli.backend.model_tools import Events, snapshot, compare, diagnose, write_report, tensor_health
from cli.backend.nn_session import TrainingSession
from cli.backend.training_run import TrainingRun
from python.train_nn import TrainingConfig


class ModelToolsTests(unittest.TestCase):
    def test_bounded_training_history_resume_and_exact_initial_actor(self):
        with tempfile.TemporaryDirectory() as directory:
            config = TrainingConfig(batch_size=2, batches=1, agents=1, device='cpu')
            with TrainingRun(config, name='history', directory=directory) as run:
                initial = {k:v.clone() for k,v in run.policy.state_dict().items()}
                session = TrainingSession(training=run)
                with contextlib.redirect_stdout(io.StringIO()):
                    session.run()
                path = run.path
            data, digest = snapshot(path)
            self.assertEqual(data['updates_completed'], 1)
            self.assertEqual([row['update'] for row in data['training_history']], [0,1])
            self.assertEqual(len(data['training_history'][1]['episodes']),2)
            for k,v in initial.items():
                self.assertTrue(torch.equal(v,data['initial_actor'][k]))
            saved_rng = data['rng_state'].clone()
            with TrainingRun(config, mode='resume', checkpoint=path, directory=directory) as run:
                self.assertTrue(torch.equal(torch.get_rng_state(),saved_rng))
                output=io.StringIO()
                with contextlib.redirect_stdout(output):
                    TrainingSession(events=True,training=run)
                self.assertIn('AURA_NN\thistory_update\t1',output.getvalue())
                self.assertIn('AURA_NN\thistory_episode\t2',output.getvalue())
                self.assertEqual(len(run.history),2)
                run.save_current()
            restored,_=snapshot(path)
            self.assertEqual(data['training_history'],restored['training_history'])
            for key in data['model']:
                self.assertTrue(torch.equal(data['model'][key],restored['model'][key]))
            # Pure reports do not need evaluation or any checkpoint write.
            before=path.read_bytes()
            report=write_report(path,restored,digest,{},'report',Path(directory)/'reports')
            payload=json.loads(report.with_suffix('.json').read_text())
            self.assertEqual(payload['history'],data['training_history'])
            self.assertEqual(path.read_bytes(),before)
            self.assertEqual(len(report.with_suffix('.csv').read_text().splitlines()),3)

    def test_comparison_matches_streams_preserves_snapshot_and_individual_results(self):
        with tempfile.TemporaryDirectory() as directory:
            with TrainingRun(TrainingConfig(agents=1), name='readonly',directory=directory) as run:
                path=run.save_current()
            data,digest=snapshot(path)
            before=path.read_bytes()
            calls=[]
            def evaluation(env,policy,stochastic=False,seed=10042):
                calls.append((stochastic,seed))
                self.assertFalse(any(p.grad is not None for p in policy.parameters()))
                return dict(steps=100,balanced_steps=20,balanced_seconds=20/120,
                    total_reward=5.,fall_rate=1.,termination_reason='fallen')
            events=Events()
            with patch('cli.backend.model_tools.evaluate_balance',side_effect=evaluation), contextlib.redirect_stdout(io.StringIO()):
                result=compare(path,data,digest,events)
            self.assertEqual(calls[:4],calls[4:])
            self.assertEqual(len(result['results']),2) # initial + latest; no missing best invented
            self.assertEqual(len(result['results'][0]['stochastic']),3)
            self.assertEqual(path.read_bytes(),before)
            self.assertEqual(result['results'][0]['provenance'],'Saved initial actor')

    def test_unrecorded_history_is_explicit_and_exports_are_unique(self):
        with tempfile.TemporaryDirectory() as directory:
            with TrainingRun(TrainingConfig(agents=1),name='old',directory=directory) as run:
                path=run.save_current()
            data,digest=snapshot(path)
            data.pop('training_history')
            a=write_report(path,data,digest,{},'report',directory)
            b=write_report(path,data,digest,{},'report',directory)
            self.assertNotEqual(a,b)
            self.assertIn('unavailable',a.read_text())
            self.assertEqual(json.loads(a.with_suffix('.json').read_text())['history_summary']['recorded_updates'],0)

    def test_invalid_contract_is_rejected_before_any_evaluation(self):
        with tempfile.TemporaryDirectory() as directory:
            with TrainingRun(TrainingConfig(agents=1),name='bad',directory=directory) as run:
                path=run.save_current()
            data,digest=snapshot(path)
            data['reward_version']=1
            with patch('cli.backend.model_tools.evaluate_balance') as evaluate:
                with self.assertRaises(ValueError):
                    compare(path,data,digest,Events())
                evaluate.assert_not_called()

    def test_health_detects_nonfinite_weights(self):
        self.assertFalse(tensor_health({'weight':torch.tensor([float('nan')])})['finite'])
        self.assertTrue(tensor_health({'weight':torch.tensor([1.])})['finite'])

    def test_default_exports_are_grouped_by_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            worker = root / 'cli' / 'backend' / 'model_tools.py'
            with patch('cli.backend.model_tools.__file__', str(worker)):
                for mode in ('compare', 'diagnose', 'report'):
                    report = write_report(root / 'actor.pt', {}, 'fixture', {}, mode)
                    self.assertEqual(report.parent, (root / 'reports' / mode).resolve())
                    self.assertTrue(report.with_suffix('.json').is_file())
                    self.assertTrue(report.with_suffix('.csv').is_file())


if __name__=='__main__':
    unittest.main()
