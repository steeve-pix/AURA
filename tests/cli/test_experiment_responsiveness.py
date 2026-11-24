"""Regressions for modal traps, pause/resume, and idle-history work."""
import json
from pathlib import Path
import tempfile
import os
import pty
import termios
import unittest
from unittest.mock import patch
from cli.backend.experiment_workspace import RecordCache,read_record,history
from cli.backend.terminal_screen import responsive_input
from python.experiment_library import configuration,create_experiment,rename_experiment
from tests.cli import test_experiment_ui


class ResponsiveScreen(test_experiment_ui.Screen):
    def __init__(self,keys):super().__init__(keys,120,40);self.timeouts=[]
    def timeout(self,value):self.timeouts.append(value)


class CellScreen(ResponsiveScreen):
    def addnstr(self,y,x,text,n,attrs):
        cells=list(self.lines.get(y,'').ljust(self.width))
        cells[x:x+len(text[:n])]=text[:n]
        self.lines[y]=''.join(cells).rstrip()


class ResponsivenessTests(unittest.TestCase):
    def test_rename_completed_experiment_preserves_identity_settings_and_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            path=create_experiment(configuration('original'),directory)
            (path/'state.json').write_text('{"status":"completed"}')
            (path/'results.json').write_text('{"rows":[],"elapsed_seconds":12}')
            evidence={p.name:p.read_bytes() for p in path.iterdir() if p.name!='configuration.json'}
            original=json.loads((path/'configuration.json').read_text())
            self.assertEqual(rename_experiment(path,'renamed'),path)
            self.assertEqual(json.loads((path/'configuration.json').read_text()),dict(original,name='renamed'))
            for name,payload in evidence.items():self.assertEqual((path/name).read_bytes(),payload)
            self.assertEqual(len(list(Path(directory).iterdir())),1)
            with self.assertRaises(ValueError):rename_experiment(path,'bad/name')
            self.assertEqual(json.loads((path/'configuration.json').read_text())['name'],'renamed')

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_rename_dialog_saves_label_without_copy_and_escape_cancels(self,_):
        with tempfile.TemporaryDirectory() as directory:
            path=create_experiment(configuration('original'),directory)
            row=read_record(path)[0]
            ui=self.ui([21,*map(ord,'renamed'),10]);context=ui.workspace_context
            ui.rename_experiment(row)
            self.assertEqual(read_record(path)[0]['name'],'renamed')
            self.assertEqual(ui.workspace_context,context)
            ui=self.ui([21,ord('x'),27]);ui.rename_experiment(read_record(path)[0])
            self.assertEqual(read_record(path)[0]['name'],'renamed')

    def ui(self,keys):
        ui=test_experiment_ui.ExperimentFormTests().ui([])
        ui.screen=ResponsiveScreen(keys);ui.directory=Path('/unused')
        ui.workspace_context=('workspace',3)
        return ui

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_empty_picker_has_own_title_exit_keys_and_restores_workspace(self,_):
        ui=self.ui([ord('q')]);context=ui.workspace_context
        self.assertIsNone(ui.choose('SAVED AUDITS',[]))
        shown=' '.join(ui.screen.frames[0].values())
        self.assertIn('SAVED AUDITS',shown)
        self.assertIn('Esc cancel',shown)
        self.assertNotIn('[A] Audit actors',shown)
        self.assertEqual(ui.workspace_context,context)

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_confirmation_does_not_hide_yes_no_and_q_cancels(self,_):
        ui=self.ui([ord('q')]);context=ui.workspace_context
        self.assertFalse(ui.confirm('RUN BOUNDED EXPERIMENT',['Review']))
        shown=' '.join(ui.screen.frames[0].values())
        self.assertIn('[Y] confirm',shown)
        self.assertIn('[N]',shown)
        self.assertEqual(ui.workspace_context,context)

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_pause_n_resumes_and_double_q_cancels_without_blocking_running_input(self,_):
        ui=self.ui([ord('p'),ord('n'),ord('q'),ord('q')]);events=[]
        class Environment:
            def set_paused(self,value):events.append(value)
        def execute(path,*,service,stage,progress):
            service.environment=Environment()
            service()  # P then N must return from the paused loop.
            service()  # First Q requests cancellation.
            service()  # Second Q raises, caught by the screen.
            self.fail('Cancellation should stop execution')
        with patch('cli.backend.experiment_ui.run_experiment',side_effect=execute):
            ui.run(dict(path='/unused',configuration=configuration()),confirmed=True)
        self.assertEqual(events,[True,False])
        self.assertIn('Cancelled',ui.message)
        self.assertIn(0,ui.screen.timeouts)
        self.assertIn(100,ui.screen.timeouts)
        self.assertEqual(ui.screen.timeouts[-1],-1)

    def test_unchanged_history_is_cached_and_atomic_replacement_is_seen(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'configuration.json').write_text(json.dumps(configuration()))
            (root/'state.json').write_text(json.dumps(dict(status='draft')))
            cache=RecordCache()
            with patch('cli.backend.experiment_workspace.read_record',wraps=read_record) as read:
                cache.read(root);cache.read(root)
                self.assertEqual(read.call_count,1)
                temporary=root/'new-state.json';temporary.write_text(json.dumps(dict(status='completed')))
                temporary.replace(root/'state.json')
                self.assertEqual(cache.read(root)[0]['status'],'completed')
                self.assertEqual(read.call_count,2)

    def test_flow_control_disabled_during_screen_and_restored_after_error(self):
        master,slave=pty.openpty()
        try:
            saved=termios.tcgetattr(slave);saved[0]|=termios.IXON
            termios.tcsetattr(slave,termios.TCSANOW,saved)
            class Input:
                def fileno(self):return slave
            def failure(screen):
                self.assertFalse(termios.tcgetattr(slave)[0]&termios.IXON)
                raise ValueError('fixture failure')
            with patch('cli.backend.terminal_screen.sys.stdin',Input()),patch('cli.backend.terminal_screen.curses.set_escdelay'):
                with self.assertRaisesRegex(ValueError,'fixture failure'):responsive_input(failure,None)
            self.assertEqual(termios.tcgetattr(slave),saved)
        finally:os.close(master);os.close(slave)

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_history_toggle_uses_correct_metrics_and_does_not_overwrite_footer(self,_):
        ui=self.ui([])
        ui.workspace_context=('workspace',2)
        rows=[dict(branch='control',seed=42,updates=[dict(update=i,evaluation=dict(
            mean=dict(balanced_seconds=.25,fall_rate=1.),deterministic=dict(balanced_seconds=.75,fall_rate=0.))) for i in range(1,60)])]
        history(ui,rows,'control',0,True)
        shown=' '.join(ui.screen.lines.values())
        self.assertIn('0.750',shown)
        self.assertNotIn('0.250',shown)
        self.assertIn('─',ui.screen.lines[ui.content_bottom])

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_long_lists_stop_above_wrapped_shortcuts(self,_):
        ui=test_experiment_ui.ExperimentFormTests().ui([],64,24)
        ui.page('LIBRARY')
        ui.action_footer([('[N] New',True),('[Enter] Inspect',True),('[C] Copy',True),('[E] Edit',True),('[B] Benchmarks',True),('[R] Run draft',True),('[P] Promote',True),('[U] Restore',True),('[Q] Back',True),('[D] Delete',True)])
        ui.rows([f'completed / item {i}' for i in range(100)],99)
        self.assertIn('─',ui.screen.lines[ui.content_bottom])
        self.assertIn('of 100',ui.screen.lines[ui.content_bottom-1])

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_group_tabs_replace_old_context_without_leftover_letters(self,_):
        ui=self.ui([]);ui.screen=CellScreen([14,27])
        self.assertIsNone(ui.form(configuration(),grouped=True))
        for frame in ui.screen.frames:
            self.assertEqual(frame[3].strip(),'Study   Network   Learning   Reward / validation')


if __name__=='__main__':unittest.main()
