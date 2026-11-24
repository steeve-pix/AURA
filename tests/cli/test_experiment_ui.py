"""Focused form checks at small terminal sizes without launching training."""
import unittest
from unittest.mock import patch
from cli.backend.experiment_ui import LibraryUI,input_layout,LABELS,parse_list
from python.experiment_library import configuration,inactive_fields
import curses

class Screen:
    def __init__(self,keys,width=64,height=24):self.keys=iter(keys);self.width=width;self.height=height;self.frames=[];self.lines={}
    def getmaxyx(self):return self.height,self.width
    def erase(self):self.lines={}
    def addnstr(self,y,x,text,n,attrs):
        assert 0<=y<self.height and 0<=x<self.width
        assert len(text[:n])+x<self.width
        self.lines[y]=self.lines.get(y,'')+text[:n]
    def refresh(self):self.frames.append(dict(self.lines))
    def getch(self):return next(self.keys)

class ExperimentFormTests(unittest.TestCase):
    def ui(self,keys,width=64,height=24):
        ui=LibraryUI.__new__(LibraryUI);ui.screen=Screen(keys,width,height);ui.message='';return ui
    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_shortcuts_anchor_to_bottom_with_separator(self,_):
        for width,height in ((64,24),(120,40),(200,60)):
            ui=self.ui([],width,height)
            ui.action_footer([('[N] New',True),('[Enter] Inspect',False),('[C] Copy',False),
                              ('[R] Run draft',False),('[Q] Back',True),('[D] Delete',False)])
            self.assertIn('Delete',ui.screen.lines[height-2])
            rule=next(y for y,line in ui.screen.lines.items() if '─' in line)
            self.assertLess(rule,height-2)
            self.assertTrue(all(y>rule for y,line in ui.screen.lines.items() if '[' in line))
            ui.page('MODEL BUILDER','Tab field / Enter review / Esc cancel')
            self.assertIn('cancel',ui.screen.lines[height-2])
            self.assertIn('─',ui.screen.lines[height-3])

    def test_confirmations_expire_but_errors_remain(self):
        ui=self.ui([])
        with patch('cli.backend.experiment_ui.time.monotonic',return_value=100):
            ui.message='Restored the last deleted item'
        with patch('cli.backend.experiment_ui.time.monotonic',return_value=104):
            self.assertEqual(ui.message,'Restored the last deleted item')
        with patch('cli.backend.experiment_ui.time.monotonic',return_value=106):
            self.assertEqual(ui.message,'')
            ui.set_message('Cannot restore an existing item',error=True)
        with patch('cli.backend.experiment_ui.time.monotonic',return_value=1000):
            self.assertEqual(ui.message,'Cannot restore an existing item')

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_experiment_dashboard_has_cards_progress_and_measured_rows(self,_):
        metrics=dict(balanced_seconds=.44,balanced_fraction=.12,longest_balanced_steps=53,
            recoveries=0,average_balance_quality=.126,total_reward=55.2,steps=442.6,fall_rate=1.)
        rows=[dict(branch='control',seed=42,label='control / seed 42 / complete',final=dict(mean=metrics))]
        for width,height in ((64,24),(100,34),(160,40),(220,60)):
            ui=self.ui([],width,height)
            ui.experiment_overview(configuration(),metrics,rows,phase='control / seed 42',status='running',
                elapsed=14,percent=33.3,remaining=29,live=True)
            shown=' '.join(ui.screen.lines.values())
            self.assertIn('Balanced time',shown)
            self.assertIn('33.3%',shown)
            self.assertIn('Estimated remaining',shown)
            self.assertIn('STOCHASTIC',shown)
            self.assertNotIn('balanced_seconds:',shown)
            self.assertNotIn('Before → after',shown)
            if width>=150:self.assertIn('ACTOR / motor policy',shown)

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_completed_workspace_fixed_tabs_and_branch_selection(self,_):
        import tempfile,json
        from pathlib import Path
        metrics=dict(balanced_seconds=.44,balanced_fraction=.12,longest_balanced_steps=53,
            recoveries=0,average_balance_quality=.126,total_reward=55.2,steps=442.6,fall_rate=1.)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)
            (path/'configuration.json').write_text(json.dumps(configuration()))
            (path/'state.json').write_text(json.dumps(dict(status='completed')))
            (path/'results.json').write_text(json.dumps(dict(rows=[dict(branch=b,seed=42,
                initial=dict(mean=metrics,deterministic=metrics),
                final=dict(mean=metrics,deterministic=metrics)) for b in ('control','candidate')])))
            ui=self.ui([ord('2'),9,ord('m'),ord('3'),ord('4'),ord('5'),ord('q')],200,50)
            ui.directory=path
            ui.details(dict(path=str(path),configuration=configuration(),status='completed'))
            frames=[' '.join(frame.values()) for frame in ui.screen.frames]
            self.assertIn('SETUP / STUDY',frames[0])
            self.assertIn('control / mean',frames[1])
            self.assertIn('candidate / mean',frames[2])
            self.assertIn('DETERMINISTIC',frames[3])
            self.assertIn('REWARD AUDIT',frames[4])
            self.assertIn('measured branch means',frames[5])
            self.assertIn('SAVED RUNS',frames[6])
            self.assertTrue(all('[5] Runs' in frame for frame in frames))
            self.assertIsNone(ui.workspace_context)

    def test_new_builder_uses_editable_json_lists(self):
        ui=self.ui([])
        with patch.object(ui,'form',return_value=None) as form:ui.new_template()
        self.assertIsInstance(form.call_args.args[0]['actor_hidden'],list)
        self.assertIsInstance(form.call_args.args[0]['critic_hidden'],list)

    def test_edit_draft_saves_in_place_and_cancel_preserves_settings(self):
        ui=self.ui([]);ui.directory='/unused';row=dict(path='/unused/draft',status='draft',configuration=configuration())
        changed=dict(row['configuration'],updates=10)
        with patch.object(ui,'form',return_value=changed) as form,patch.object(ui,'confirm',return_value=True),patch('cli.backend.experiment_ui.update_experiment') as save:
            ui.edit_experiment(row)
            form.assert_called_once_with(row['configuration'],editing=True,grouped=True)
            save.assert_called_once_with(row['path'],changed)
        with patch.object(ui,'form',return_value=None),patch('cli.backend.experiment_ui.update_experiment') as save:
            ui.edit_experiment(row)
            save.assert_not_called()

    def test_edit_completed_experiment_creates_copy(self):
        ui=self.ui([]);row=dict(status='completed',configuration=configuration())
        with patch.object(ui,'new_experiment') as copy,patch('cli.backend.experiment_ui.update_experiment') as save:
            ui.edit_experiment(row)
            copy.assert_called_once_with(row['configuration'])
            save.assert_not_called()

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_actor_checklist_selects_multiple_and_excludes_control_and_snapshots(self,_):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for filename in ('a.pt','b.pt','c.pt','source_snapshot.pt','compared_source_hidden.pt'):
                (root/filename).touch()
            ui=self.ui([32,curses.KEY_DOWN,32,10],120,40)
            with patch('python.model_checkpoint.MODEL_DIR',root):
                selected=ui.checkpoint_picker(multiple=True,exclude=str(root/'a.pt'))
            self.assertEqual(selected,[str(root/'b.pt'),str(root/'c.pt')])

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_layer_remove_then_add_keeps_distinct_fields(self,_):
        ui=self.ui([ord('D'),ord('A'),10])
        self.assertEqual(ui.numeric_editor('HIDDEN LAYERS',{'0':32,'1':128},True),[128,64])
    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_left_right_dont_edit_text_and_tab_reaches_empty_fields(self,_):
        ui=self.ui([21,ord('a'),ord('b'),curses.KEY_LEFT,ord('c'),9,9,10])
        result=ui.form(dict(name='original',actor_hidden=[8],critic_hidden=[8],actor_activation='Tanh',critic_activation='Tanh',initialization='default'),True)
        self.assertEqual(result['name'],'abc')
        self.assertTrue(any('Actor hidden' in ''.join(frame.values()) for frame in ui.screen.frames))
    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_review_scrolls_and_keeps_confirmation_controls_visible(self,_):
        for width in (64,120):
            ui=self.ui([curses.KEY_DOWN,ord('n')],width)
            self.assertFalse(ui.confirm('REVIEW',['A long line '*40]*20))
            self.assertTrue(all('confirm' in ''.join(frame.values()) and 'cancel' in ''.join(frame.values()) for frame in ui.screen.frames))
    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_large_forms_show_all_inputs_and_live_network_preview(self,_):
        for width,height in ((120,40),(200,60)):
            data=configuration();data['kind']='training'
            ui=self.ui([27],width,height)
            self.assertIsNone(ui.form(data))
            shown=' '.join(ui.screen.frames[0].values())
            keys=[k for k in data if k not in ('schema_version','criteria') and k not in inactive_fields('training')]+list(data['criteria'])
            self.assertGreaterEqual(input_layout(len(keys),width,height)['capacity'],len(keys))
            for key in keys:self.assertIn(('Experiment name' if key=='name' else LABELS[key])[:30],shown)
            self.assertIn('ACTOR / motor policy',shown)
            self.assertIn('CRITIC / state value',shown)
            self.assertIn('[205] → [64] → [64] → [32]',shown)
            self.assertNotIn('Page 1/',shown)

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_small_forms_use_fixed_pages_instead_of_scrolling_per_field(self,_):
        data=configuration();data['kind']='training'
        capacity=input_layout(27,64,24)['capacity']
        ui=self.ui([9]*capacity+[27])
        ui.form(data)
        self.assertIn('Page 1/',' '.join(ui.screen.frames[0].values()))
        self.assertIn('Page 1/',' '.join(ui.screen.frames[capacity-1].values()))
        self.assertIn('Page 2/',' '.join(ui.screen.frames[capacity].values()))

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_network_preview_uses_selected_widths_and_parameter_counts(self,_):
        ui=self.ui([],140,50)
        ui.network_preview(dict(actor_hidden='[128]',critic_hidden='[]',actor_activation='ReLU',critic_activation='Tanh'),90)
        shown=' '.join(ui.screen.lines.values())
        self.assertIn('[205] → [128] → [32]',shown)
        self.assertIn('[205] → [1]',shown)
        self.assertIn('Hidden: ReLU',shown)
        self.assertIn('30,496 parameters',shown)

    def test_friendly_numeric_lists_keep_nested_json_and_reject_bad_input(self):
        for text in ('64 64 64','64,64,64','[64, 64, 64]','[64 64 64]'):
            self.assertEqual(parse_list(text),[64,64,64])
        self.assertEqual(parse_list('0.001 0.0001'),[.001,.0001])
        self.assertEqual(parse_list('[[64,64],[128,128]]'),[[64,64],[128,128]])
        for text in ('64 invalid 64','[64,64','64,,64'):
            with self.assertRaises(ValueError):parse_list(text)

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_spaces_can_be_typed_in_hidden_layer_field_without_opening_editor(self,_):
        ui=self.ui([9,21,*map(ord,'64 64 64'),9,10])
        with patch.object(ui,'numeric_editor',side_effect=AssertionError('Space must enter text')):
            result=ui.form(dict(name='three_layers',actor_hidden=[8],critic_hidden=[8],actor_activation='Tanh',critic_activation='Tanh',initialization='default'),True)
        self.assertEqual(result['actor']['hidden_sizes'],[64,64,64])
        self.assertTrue(any('[64, 64, 64]' in ''.join(frame.values()) for frame in ui.screen.frames))

if __name__=='__main__':unittest.main()
