"""Command entry points must parse before any training or checkpoint writes."""
import contextlib
import importlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


class StartupTests(unittest.TestCase):
    def test_all_backend_help_commands_parse(self):
        for name in ('nn_session','critic_session','evaluate_model','model_viewer',
                     'model_tools','experiment_ui','saved_library'):
            with self.subTest(command=name):
                module=importlib.import_module('cli.backend.'+name)
                with patch('sys.argv',[name,'--help']),contextlib.redirect_stdout(io.StringIO()):
                    with self.assertRaises(SystemExit) as stopped:
                        module.main()
                self.assertEqual(stopped.exception.code,0)

    def test_new_training_loads_promoted_design_before_starting_session(self):
        from cli.backend import nn_session
        from python.model_templates import template,save_template
        with tempfile.TemporaryDirectory() as folder:
            directory=Path(folder)
            design=template('startup-fixture')
            design['actor']['hidden_sizes']=[32,16]
            path=save_template(design,directory)
            (directory/'default.json').write_text(json.dumps({'template':str(path)}))
            with patch('python.model_templates.TEMPLATE_DIR',directory), \
                 patch.object(nn_session,'TrainingRun') as run, \
                 patch.object(nn_session,'TrainingSession') as session, \
                 patch('sys.argv',['nn_session','--events','--mode','new','--agents','1',
                                   '--episodes-per-batch','2','--batches','1']):
                nn_session.main()
            config=run.call_args.args[0]
            self.assertEqual(list(config.actor_design.hidden_sizes),[32,16])
            self.assertEqual(config.batch_size,2)
            session.return_value.run.assert_called_once()
