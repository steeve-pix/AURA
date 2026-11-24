import fcntl
import json
from pathlib import Path
import tempfile
import unittest
import torch
from cli.backend.saved_library import catalog,trash,restore_last
from python.model_templates import save_template,template

class SavedLibraryTests(unittest.TestCase):
    def test_edit_draft_preserves_identity_and_snapshot_and_rejects_started_runs(self):
        from python.experiment_library import configuration,create_experiment,update_experiment
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'source.pt';torch.save({},source);original_source=source.read_bytes()
            config=dict(configuration(),kind='training',source=str(source))
            path=create_experiment(config,root/'experiments')
            state=(path/'state.json').read_bytes()
            torch.save({'changed':True},source)
            updated=dict(config,name='renamed',updates=10)
            self.assertEqual(update_experiment(path,updated),path)
            self.assertEqual((path/'state.json').read_bytes(),state)
            self.assertEqual((path/'source_snapshot.pt').read_bytes(),original_source)
            self.assertEqual(json.loads((path/'configuration.json').read_text())['updates'],10)
            before=(path/'configuration.json').read_bytes()
            with (path/'.run.lock').open('a') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
                with self.assertRaisesRegex(ValueError,'running'):update_experiment(path,config)
            (path/'state.json').write_text(json.dumps(dict(status='completed')))
            with self.assertRaisesRegex(ValueError,'Only drafts'):update_experiment(path,config)
            self.assertEqual((path/'configuration.json').read_bytes(),before)

    def test_edit_draft_replaces_or_removes_source_snapshot(self):
        from python.experiment_library import configuration,create_experiment,update_experiment
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);config=dict(configuration(),kind='training');path=create_experiment(config,root/'experiments')
            source=root/'source.pt';torch.save({},source)
            update_experiment(path,dict(config,source=str(source)))
            self.assertEqual((path/'source_snapshot.pt').read_bytes(),source.read_bytes())
            update_experiment(path,config)
            self.assertFalse((path/'source_snapshot.pt').exists())
            self.assertFalse((path/'source.json').exists())

    def test_design_delete_and_restore_preserve_exact_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);path=save_template(template('design'),root/'templates');before=path.read_bytes()
            receipt=trash(path,root);self.assertFalse(path.exists());self.assertEqual((receipt/'item').read_bytes(),before)
            self.assertEqual(restore_last(root),path.resolve());self.assertEqual(path.read_bytes(),before)

    def test_active_checkpoint_and_experiment_are_protected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);actor=root/'actor.pt';actor.write_bytes(b'original')
            experiment=root/'experiments/run';experiment.mkdir(parents=True)
            for path,lockpath in ((actor,root/'.actor.pt.lock'),(experiment,experiment/'.run.lock')):
                with lockpath.open('a') as lock:
                    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
                    with self.assertRaisesRegex(ValueError,'in use'):trash(path,root)
                    self.assertTrue(path.exists())

    def test_restore_never_overwrites_and_only_selected_item_is_removed(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);path=root/'actor.pt';best=root/'actor_best.pt';path.write_bytes(b'actor');best.write_bytes(b'best')
            trash(path,root);self.assertEqual(best.read_bytes(),b'best');path.write_bytes(b'new actor')
            with self.assertRaisesRegex(ValueError,'already exists'):restore_last(root)
            self.assertEqual(path.read_bytes(),b'new actor')

    def test_default_design_outside_paths_and_symlinks_are_protected(self):
        with tempfile.TemporaryDirectory() as folder,tempfile.TemporaryDirectory() as outside:
            root=Path(folder);path=save_template(template('default_design'),root/'templates')
            (root/'templates/default.json').write_text(json.dumps({'template':str(path)}))
            with self.assertRaisesRegex(ValueError,'default'):trash(path,root)
            escaped=Path(outside)/'escape.pt';escaped.write_bytes(b'keep')
            with self.assertRaises(ValueError):trash(escaped,root)
            alias=root/'alias.pt';alias.symlink_to(escaped)
            with self.assertRaises(ValueError):trash(alias,root)
            self.assertEqual(escaped.read_bytes(),b'keep')

    def test_experiment_folder_round_trip_retains_evidence(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);path=root/'experiments/run';path.mkdir(parents=True)
            (path/'results.json').write_bytes(b'evidence');(path/'actor.pt').write_bytes(b'weights')
            trash(path,root);restored=restore_last(root)
            self.assertEqual((restored/'results.json').read_bytes(),b'evidence')
            self.assertEqual((restored/'actor.pt').read_bytes(),b'weights')

    def test_catalog_keeps_saved_designs_and_experiments_separate(self):
        from python.experiment_library import configuration
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);save_template(template('design'),root/'templates')
            path=root/'experiments/run';path.mkdir(parents=True)
            (path/'configuration.json').write_text(json.dumps(configuration('study')))
            (path/'state.json').write_text(json.dumps({'status':'draft'}))
            rows=list(catalog(root));self.assertEqual({r['kind'] for r in rows},{'design','experiment'})
            self.assertIn('Untrained design',rows[0]['details'][0])
            self.assertEqual(rows[1]['status'],'draft')
if __name__=='__main__':unittest.main()
