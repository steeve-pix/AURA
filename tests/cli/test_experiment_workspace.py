"""Workspace persistence and matched actor selection without touching real models."""
import fcntl
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import curses
import torch
from python.experiment_library import configuration
from cli.backend.experiment_workspace import audit_configuration,create_actor_audit,linked_records,read_record
from tests.cli import test_experiment_ui


class WorkspaceTests(unittest.TestCase):
    def fixture(self,path):
        config=configuration('existing-training');config.update(kind='training',updates=30)
        (path/'configuration.json').write_text(json.dumps(config))
        (path/'state.json').write_text(json.dumps(dict(status='completed')))
        rows=[]
        for seed in (42,43,44):
            checkpoint=path/f'control-seed{seed}.pt';torch.save({},checkpoint)
            rows.append(dict(branch='control',seed=seed,updates=[dict(update=30)],checkpoint=str(checkpoint)))
        (path/'results.json').write_text(json.dumps(dict(rows=rows)))

    def test_one_action_finds_all_actors_and_preserves_original_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            before={p.name:p.read_bytes() for p in root.iterdir() if p.is_file()}
            config=audit_configuration(root)
            self.assertEqual(config['kind'],'reward_audit')
            self.assertEqual(Path(config['source']).name,'control-seed42.pt')
            self.assertEqual([Path(p).name for p in config['values']],['control-seed43.pt','control-seed44.pt'])
            self.assertEqual(config['training_seeds'],[42,43,44])
            audit=create_actor_audit(root)
            self.assertEqual(audit.parent,root/'audits')
            self.assertEqual(read_record(audit)[0]['status'],'draft')
            self.assertEqual(len(linked_records(root)),2)
            self.assertEqual(before,{name:(root/name).read_bytes() for name in before})

    def test_moved_experiment_finds_local_checkpoints(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            data=json.loads((root/'results.json').read_text())
            for row in data['rows']:row['checkpoint']='/old/location/'+Path(row['checkpoint']).name
            (root/'results.json').write_text(json.dumps(data))
            self.assertEqual(len(audit_configuration(root)['values']),2)

    def test_missing_actor_or_active_training_does_not_create_partial_audit(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            with (root/'.run.lock').open('a') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
                with self.assertRaisesRegex(ValueError,'still using'):create_actor_audit(root)
            (root/'control-seed43.pt').unlink()
            with self.assertRaisesRegex(ValueError,'Missing trained'):create_actor_audit(root)
            self.assertFalse((root/'audits').exists())

    def test_linked_execution_locks_parent_against_delete(self):
        from python.experiment_library import run_experiment
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);audit=create_actor_audit(root)
            def execution(*args,**kwargs):
                with (root/'.run.lock').open('a') as lock:
                    with self.assertRaises(BlockingIOError):fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
                return []
            with patch('python.experiment_library._run_experiment',side_effect=execution):
                self.assertEqual(run_experiment(audit),[])

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_audit_shortcut_creates_and_runs_all_actors_inside_workspace(self,_):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            ui=test_experiment_ui.ExperimentFormTests().ui([ord('3'),ord('a'),ord('q')],180,50)
            with patch.object(ui,'confirm',return_value=True),patch.object(ui,'run') as run,patch.object(ui,'checkpoint_picker',side_effect=AssertionError('No manual path selection')):
                ui.details(read_record(root)[0])
                audit_row=run.call_args.args[0]
                self.assertEqual(Path(audit_row['path']).parent,root/'audits')
                self.assertEqual(len(audit_row['configuration']['values']),2)
                self.assertEqual(run.call_args.kwargs,dict(confirmed=True))
            self.assertIsNone(ui.workspace_context)

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_grouped_editor_preserves_hidden_settings(self,_):
        ui=test_experiment_ui.ExperimentFormTests().ui([curses.KEY_F3,21,ord('1'),ord('2'),ord('8'),10],120,40)
        config=configuration();result=ui.form(config,grouped=True)
        self.assertEqual(result['actor_hidden'],[128])
        self.assertEqual(result['training_seeds'],config['training_seeds'])
        self.assertEqual(result['reward_parameters'],config['reward_parameters'])
        self.assertTrue(any('NETWORK / 2/4' in ' '.join(frame.values()) for frame in ui.screen.frames))

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_tabs_stay_visible_on_short_narrow_terminal(self,_):
        ui=test_experiment_ui.ExperimentFormTests().ui([],64,24)
        ui.workspace_context=('fixture',3);ui.page('')
        line=ui.screen.lines[3]
        self.assertIn('[1] Setup',line);self.assertIn('[5] Runs',line)


if __name__=='__main__':unittest.main()
