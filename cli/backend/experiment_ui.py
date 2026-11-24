"""Terminal library for model designs and bounded, reproducible experiments."""
import argparse
import curses
import json
from pathlib import Path
import time
import textwrap
import sys
import os
import re
from functools import wraps

from python.experiment_library import (LIBRARY,KINDS,COMPARISONS,configuration,validate,create_experiment,
    library,run_experiment,promote,capabilities,inactive_fields,update_experiment,rename_experiment,branches as experiment_branches)
from cli.backend.saved_library import trash,restore_last
from cli.backend.terminal_screen import run_screen
from python.worker_config import worker_warning
from python.model_templates import TEMPLATE_DIR,NetworkDesign,template,save_template,load_template

LABELS={'name':'Name','kind':'Experiment type','source':'Source checkpoint path','initialization_mode':'Starting weights',
 'actor_hidden':'Actor hidden layers / neurons','critic_hidden':'Critic hidden layers / neurons',
 'actor_activation':'Actor hidden activation','critic_activation':'Critic hidden activation','initialization':'Weight initialization',
 'device':'Learning device / rollout inference stays CPU', 'actor_lr':'Actor learning rate','critic_lr':'Critic learning rate','episodes':'Episodes per update','updates':'Training updates',
 'workers':'Independent workers','algorithm':'Training method','critic_fits':'Critic fits per update','reward_formula':'Reward formula',
 'reward_parameters':'Reward parameter overrides','evaluation_seeds':'Evaluation seeds','training_seeds':'Training seeds',
 'starting_condition':'Starting condition','comparison':'Setting to compare','values':'Comparison values',
 'min_balanced_seconds':'Minimum mean balanced seconds','max_fall_rate':'Maximum fall rate','minimum_seeds':'Minimum training seeds'}
CAPABILITIES=capabilities()
OPTIONS={'device':['cpu','gpu'],'kind':list(KINDS),'initialization_mode':['fresh','transfer'],'actor_activation':CAPABILITIES['activations'],
 'critic_activation':CAPABILITIES['activations'],'initialization':list(CAPABILITIES['initializations']),
 'algorithm':list(CAPABILITIES['algorithms']),'reward_formula':list(CAPABILITIES['reward_formulas']),
 'starting_condition':['neutral'],'comparison':list(COMPARISONS)}


def parse_list(text):
    """Accept numeric lists typed with spaces/commas as well as JSON arrays."""
    text=text.strip()
    if not text:return []
    try:
        result=json.loads(text)
        if isinstance(result,list):return result
        if type(result) in (int,float):return [result]
    except ValueError:pass
    if text.startswith('[') and text.endswith(']'):text=text[1:-1].strip()
    if not text:return []
    if any(char in text for char in '[]{}') or re.search(r'^,|,$|,\s*,',text):
        raise ValueError('Use numbers separated by spaces or commas, or a valid JSON list')
    try:
        result=[json.loads(token) for token in re.split(r'[\s,]+',text)]
    except ValueError as error:
        raise ValueError('Use numbers separated by spaces or commas; nested lists need brackets and commas') from error
    if any(type(value) not in (int,float) for value in result):raise ValueError('Use numbers or a bracketed JSON list')
    return result


def input_layout(count,width,height):
    """Fixed columns, with explicit pages only when the terminal cannot fit all fields."""
    preview=42 if width>=112 else 0
    field_width=width-8-preview
    columns=max(1,min(3,field_width//34))
    available=max(2,height-12)
    stride=3 if ((count+columns-1)//columns)*3<=available else 2
    rows=max(1,available//stride)
    if count<=columns*rows:rows=max(1,(count+columns-1)//columns)
    return dict(columns=columns,rows=rows,stride=stride,capacity=columns*rows,
        cell_width=field_width//columns,preview_x=width-preview if preview else None)


def modal_screen(method):
    """Dialogs own their titles and keys; restore the workspace on every exit."""
    @wraps(method)
    def wrapped(self,*args,**kwargs):
        context=getattr(self,'workspace_context',None)
        self.workspace_context=None
        try:return method(self,*args,**kwargs)
        finally:self.workspace_context=context
    return wrapped


class LibraryUI:
    def __init__(self,screen,tab='experiments',directory=LIBRARY,templates=TEMPLATE_DIR):
        self.screen=screen;self.tab=tab;self.directory=Path(directory);self.templates=Path(templates)
        self.message='';self.selection=0
        try:curses.curs_set(0)
        except curses.error:pass
        screen.keypad(True)
        curses.start_color();curses.use_default_colors()
        self.color_enabled='NO_COLOR' not in os.environ and curses.has_colors()
        # Match TerminalCapabilities.cpp: white, lavender, sage, gold, cyan, gray, red.
        if self.color_enabled:
            colors=(231,147,151,180,116,245,174) if curses.COLORS>=256 else (curses.COLOR_WHITE,curses.COLOR_MAGENTA,curses.COLOR_GREEN,curses.COLOR_YELLOW,curses.COLOR_CYAN,curses.COLOR_WHITE,curses.COLOR_RED)
            for index,color in enumerate(colors,1):curses.init_pair(index,color,-1)
            background=236 if curses.COLORS>=256 else 8 if curses.COLORS>=16 else -1
            curses.init_pair(8,colors[0],background)
            curses.init_pair(9,colors[1],background)
            curses.init_pair(10,174 if curses.COLORS>=256 else curses.COLOR_RED,-1)

    @property
    def message(self):
        if time.monotonic() >= getattr(self,'_message_until',float('inf')):
            self._message=''
        return getattr(self,'_message','')

    @message.setter
    def message(self,value):
        self.set_message(value)

    def set_message(self,value,error=False):
        self._message=value
        self._message_until=float('inf') if error or not value else time.monotonic()+5

    def put(self,y,x,text,color=1,bold=False,limit=None):
        h,w=self.screen.getmaxyx()
        if 0<=y<h and 0<=x<w:
            try:self.screen.addnstr(y,x,str(text),max(0,min(w-x-2,limit if limit is not None else w)),(curses.color_pair(color) if getattr(self,"color_enabled",True) else 0)|(curses.A_BOLD if bold else 0))
            except curses.error:pass

    def shortcuts(self,y,x,text,enabled=True,selected=False):
        """Use the dashboard vocabulary: accented keys, muted descriptions."""
        pattern=r'(?<!\S)(\[[^]]+\]|Up/Down|Left/Right|Shift\+Tab|S-Tab|Ctrl\+[A-Z]|Tab|Space|Enter|Esc|[A-Z])(?=\s|$)'
        at=0
        for match in re.finditer(pattern,text):
            self.put(y,x+at,text[at:match.start()],8 if selected else 6,selected)
            self.put(y,x+match.start(),match.group(),9 if selected and enabled else 2 if enabled else 6,enabled)
            at=match.end()
        self.put(y,x+at,text[at:],8 if selected else 6,selected)

    def detail(self,y,text):
        label,separator,value=text.partition(':')
        if separator:
            self.put(y,4,label+separator,6)
            self.put(y,5+len(label),value,1)
        else:self.put(y,4,text,6)

    def page(self,title,hint=''):
        self.screen.erase();h,w=self.screen.getmaxyx()
        self.content_bottom=h-5
        workspace=getattr(self,'workspace_context',None)
        if workspace:
            title='EXPERIMENT / '+workspace[0]
            if not hint.startswith('[P]'):
                hint={1:'[Tab] Settings group    [D] Saved design    [F2] Rename    [E] Edit settings    [R] Run draft    [Q] Back',
                      2:'[H] History/summary    [Tab] Branch    [M] Evaluation    Up/Down rows    [A] Audit actors    [R] Run draft    [Q] Back',
                      3:'[A] Audit actors    [L] Saved audits    [R] Run draft    Up/Down metrics    [Q] Back',
                      4:'[M] Evaluation    Up/Down metrics    [Q] Back',
                      5:'Up/Down select    [Enter] Open run    [Q] Back'}[workspace[1]]
        self.put(1,3,'◇ AURA / '+title,1,True)
        context=('/create / saved model designs' if title=='MODEL DESIGNS' else
            '/experiment / saved experiments' if title=='EXPERIMENT LIBRARY' else
            'Model design / architecture and initialization' if title=='MODEL BUILDER' or getattr(self,'model_creation',getattr(self,'tab','')=='templates') else
            'Experiment / configuration and evaluation')
        if not workspace:self.put(3,3,context,6)
        if workspace:
            from .experiment_workspace import TABS
            x=3
            for index,name in enumerate(TABS if w>=90 else ('Setup','Train','Audit','Compare','Runs'),1):
                text=f'[{index}] {name}'
                self.shortcuts(3,x,text,selected=index==workspace[1]);x+=len(text)+(3 if w>=90 else 1)
        if hint:
            hints=textwrap.wrap(hint if w>=100 else hint.replace('Shift+Tab','S-Tab').replace(' / ','/').replace('    ','  '),max(1,w-7))
            self.footer_lines(hints)
        return h,w

    def footer_lines(self,lines):
        """Anchor controls above one reserved status row, with a quiet divider."""
        h,w=self.screen.getmaxyx();top=h-1-len(lines)
        self.content_bottom=top-1
        self.put(top-1,3,'─'*max(0,w-7),6)
        for i,line in enumerate(lines):self.shortcuts(top+i,3,line)
        self.put(h-1,3,self.message,4)

    def action_footer(self,actions):
        h,w=self.screen.getmaxyx();lines=[[]];used=0
        for label,enabled in actions:
            if used and used+len(label)>w-6:lines.append([]);used=0
            lines[-1].append((label,enabled));used+=len(label)+3
        top=h-1-len(lines)
        self.content_bottom=top-1
        self.put(top-1,3,'─'*max(0,w-7),6)
        for i,line in enumerate(lines):
            x=3
            for label,enabled in line:
                if label=='[D] Delete' and enabled:
                    self.put(top+i,x,'[D]',10,True);self.put(top+i,x+3,' Delete',6)
                else:self.shortcuts(top+i,x,label,enabled)
                x+=len(label)+3
        self.put(h-1,3,self.message,4)

    def rows(self,items,selected,top=6):
        h,w=self.screen.getmaxyx();bottom=getattr(self,'content_bottom',h-5)
        capacity=max(1,bottom-top)
        overflow=len(items)>capacity
        if overflow:capacity=max(1,capacity-1)
        start=max(0,selected-capacity+1)
        for offset,item in enumerate(items[start:start+capacity]):
            index=start+offset
            status=item.lstrip(' *')
            color=7 if status.startswith(('failed /','invalid /')) else 4 if status.startswith('cancelled /') else 3 if status.startswith('completed /') else 1
            if index==selected:self.put(top+offset,3,' '*(w-6),8,True)
            self.put(top+offset,3,('▸ ' if index==selected else '  ')+item,8 if index==selected else color,index==selected)
        if overflow:self.put(bottom-1,3,f'Items {start+1}–{min(len(items),start+capacity)} of {len(items)}',6,limit=w-7)

    def wait(self):
        # Wake to redraw an expired confirmation even when no key is pressed.
        if hasattr(self.screen,'timeout'):
            self.screen.timeout(250 if self.message and self._message_until!=float('inf') else -1)
        self.screen.refresh()
        return self.screen.getch()

    @modal_screen
    def notice(self,title,lines):
        scroll=0
        while True:
            h,w=self.page(title,'Up/Down scroll    Enter / Q back')
            wrapped=[part for line in lines for part in (textwrap.wrap(line,max(1,w-8)) or [''])]
            for i,line in enumerate(wrapped[scroll:scroll+max(1,h-11)]):self.detail(6+i,line)
            key=self.wait()
            if key in (10,13,27,ord('q'),ord('Q')):return
            if key==curses.KEY_DOWN:scroll=min(max(0,len(wrapped)-max(1,h-11)),scroll+1)
            if key==curses.KEY_UP:scroll=max(0,scroll-1)

    @modal_screen
    def confirm(self,title,lines):
        scroll=0
        while True:
            h,w=self.page(title,'Up/Down review    [Y] confirm    [N] / Esc cancel')
            wrapped=[part for line in lines for part in (textwrap.wrap(line,max(1,w-8)) or [''])]
            capacity=max(1,h-11)
            for i,line in enumerate(wrapped[scroll:scroll+capacity]):
                if scroll+i==0:self.put(6+i,4,line,1,True)
                else:self.detail(6+i,line)
            if len(wrapped)>capacity:self.put(h-5,4,f'Lines {scroll+1}–{min(len(wrapped),scroll+capacity)} of {len(wrapped)}',6)
            key=self.wait()
            if key in (ord('y'),ord('Y')):return True
            if key in (27,ord('n'),ord('N'),ord('q'),ord('Q')):return False
            if key==curses.KEY_DOWN:scroll=min(max(0,len(wrapped)-capacity),scroll+1)
            if key==curses.KEY_UP:scroll=max(0,scroll-1)

    @modal_screen
    def choose(self,title,choices):
        index=0
        while True:
            self.page(title,'Up/Down choose    Enter select    Esc cancel')
            self.rows(choices,index)
            if not choices:
                self.put(6,4,'No matching items available',6)
                self.put(8,4,'Press Q or Esc to return.',6)
            key=self.wait()
            if key in (27,ord('q'),ord('Q')):return None
            if key==curses.KEY_DOWN:index=min(max(0,len(choices)-1),index+1)
            elif key==curses.KEY_UP:index=max(0,index-1)
            elif key in (10,13) and choices:return index

    @modal_screen
    def checkpoint_picker(self,multiple=False,selected=None,exclude=''):
        from python.model_checkpoint import MODEL_DIR
        paths=sorted(MODEL_DIR.glob('*.pt'))+sorted((MODEL_DIR/'experiments').glob('**/*.pt'))
        paths=[p for p in paths if p.name!='source_snapshot.pt' and not p.name.startswith('compared_source_') and str(p.resolve())!=str(Path(exclude).resolve())]
        if multiple:
            chosen=set(selected or []);index=0
            while True:
                self.page('ACTORS TO COMPARE','Up/Down choose    Space select    Enter accept    Esc cancel')
                self.rows([('[x] ' if str(p) in chosen else '[ ] ')+str(p.relative_to(MODEL_DIR)) for p in paths],index)
                key=self.wait()
                if key in (27,ord('q'),ord('Q')):return None
                if key in (10,13):return [str(p) for p in paths if str(p) in chosen]
                if key==curses.KEY_DOWN:index=min(max(0,len(paths)-1),index+1)
                elif key==curses.KEY_UP:index=max(0,index-1)
                elif key==32 and paths:
                    value=str(paths[index])
                    if value in chosen:chosen.remove(value)
                    elif len(chosen)<8:chosen.add(value)
                    else:self.message='Select up to 8 comparison actors'
        index=self.choose('READ-ONLY SOURCE CHECKPOINT',[str(p.relative_to(MODEL_DIR)) for p in paths])
        return paths[index] if index is not None else None

    def network_preview(self,values,x,y=6):
        h,w=self.screen.getmaxyx();width=w-x-3
        if width<30:return
        cursor=y
        def line(text,color=6,bold=False):
            nonlocal cursor
            if cursor<h-7:self.put(cursor,x,text,color,bold,width)
            cursor+=1
        interface=template('preview')
        for role,output in (('actor',interface['actions']),('critic',1)):
            line(role.upper()+' / '+('motor policy' if role=='actor' else 'state value'),1,True)
            try:
                hidden=parse_list(values.get(role+'_hidden','[64,64]'))
                design=NetworkDesign(hidden,values.get(role+'_activation','Tanh'),values.get('initialization','default'))
                sizes=[interface['observation_size'],*design.hidden_sizes,output]
                chain=' → '.join('['+str(size)+']' for size in sizes)
                for part in textwrap.wrap(chain,width,break_long_words=False):line(part,2)
                line('Hidden: '+design.activation if design.hidden_sizes else 'Hidden: none / direct projection')
                line('Output: '+('Tanh / bounded actions' if role=='actor' else 'Linear / scalar value'))
                line(f'{sum((a+1)*b for a,b in zip(sizes,sizes[1:])):,} parameters',1)
            except (ValueError,TypeError):line('Complete the layer settings to preview',4)
            line('')
        line('SETTINGS',1,True)
        if 'kind' in values:
            line('Study: '+values['kind'])
            line('Workers: '+values.get('workers','—')+' / isolated CPU physics')
            line('Learner: '+values.get('device','cpu'))
            if values['kind'] in ('training','critic_fit'):
                line('Episodes/update: '+values.get('episodes','—'))
                line('Updates: '+values.get('updates','—'))
                line('Method: '+values.get('algorithm','actor_critic_mc') if values['kind']=='training' else 'Actor frozen / critic fitting only')
            elif values['kind']=='baseline':
                try:seeds=len(parse_list(values.get('evaluation_seeds','[]')))
                except (ValueError,TypeError):seeds='—'
                line(f'Evaluation: 1 deterministic + {seeds} stochastic')
            else:line('Collection episodes: '+values.get('episodes','—'))
            line('Reward: '+values.get('reward_formula','product'))
            line('Starting weights: '+values.get('initialization_mode','fresh'))
            if values['kind'] in ('training','critic_fit'):line('Critic: fresh / targets return ÷ 100')
            else:line('Frozen study / no learning updates')
        else:line('Untrained design / no learned weights')
        line('Initialization: '+values.get('initialization','default'))
        line('205 observations / 32 motor actions')
        if cursor>=h-7:self.put(h-7,x,'Full settings remain in the input columns',6,limit=width)

    def draw_inputs(self,keys,values,index,title=None,layer_labels=False):
        h,w=self.screen.getmaxyx();layout=input_layout(len(keys),w,h)
        capacity=layout['capacity'];page=index//capacity;start=page*capacity
        if len(keys)>capacity:
            self.put(4,3,' '*(w-7),limit=w-7)
            self.put(4,4,f'Page {page+1}/{(len(keys)+capacity-1)//capacity} / PgUp/PgDn / Tab fields',6)
        for offset,key in enumerate(keys[start:start+capacity]):
            active=start+offset==index
            column,row=divmod(offset,layout['rows']);x=4+column*layout['cell_width'];y=6+row*layout['stride']
            width=layout['cell_width']-3
            label=('Hidden layer '+str(start+offset+1)+' / neurons') if layer_labels else ('Model name' if getattr(self,'model_creation',False) else 'Experiment name') if key=='name' else LABELS.get(key,key)
            self.put(y,x,label,6,active,width)
            shown=values[key]+('▏' if active else '')
            self.put(y+layout['stride']-1,x+1,shown[-max(1,width-1):] or '—',8 if active else 1,active,width-1)
        return layout

    @modal_screen
    def numeric_editor(self,title,values,layers=False,preview_values=None,target=None):
        keys=list(values);draft={key:str(value) for key,value in values.items()};index=0;error=''
        while True:
            h,w=self.page(title,('A add layer / D remove layer / ' if layers else '')+'Tab field / Ctrl+U clear / Enter apply / Esc cancel')
            self.put(5,4,'Hidden layers only; motor-output Tanh and scalar value output stay fixed.' if layers else 'Reward normalization only / physics and termination are unchanged',6)
            layout=self.draw_inputs(keys,draft,index,layer_labels=layers)
            if preview_values is not None and layout['preview_x'] is not None:
                preview=dict(preview_values)
                try:preview[target]=json.dumps([int(draft[k]) for k in keys] if layers else {k:float(draft[k]) for k in keys})
                except ValueError:preview[target]=''
                self.network_preview(preview,layout['preview_x'])
            if not keys:self.put(8,4,'No hidden layers / direct input-to-output network / A adds one',6)
            self.put(h-1,3,error,7)
            key=self.wait()
            if key==27:return None
            if key==curses.KEY_NPAGE and keys:index=min(len(keys)-1,(index//layout['capacity']+1)*layout['capacity'])
            elif key==curses.KEY_PPAGE and keys:index=max(0,(index//layout['capacity']-1)*layout['capacity'])
            elif key in (9,curses.KEY_DOWN) and keys:index=(index+1)%len(keys)
            elif key in (curses.KEY_BTAB,curses.KEY_UP) and keys:index=(index-1)%len(keys)
            elif layers and key in (ord('a'),ord('A')):
                if len(keys)>=CAPABILITIES['hidden_layer_limit']:error='Hidden-layer limit reached'
                else:
                    name=str(max((int(k) for k in keys),default=-1)+1);keys.append(name);draft[name]='64';index=len(keys)-1
            elif layers and key in (ord('d'),ord('D')) and keys:
                keys.pop(index);index=min(index,max(0,len(keys)-1))
            elif key==21 and keys:draft[keys[index]]=''
            elif key in (127,8,curses.KEY_BACKSPACE) and keys:draft[keys[index]]=draft[keys[index]][:-1]
            elif key in (10,13):
                try:
                    if layers:
                        result=[int(draft[name]) for name in keys];NetworkDesign(result);return result
                    return {name:float(draft[name]) for name in keys}
                except ValueError as exc:error=str(exc)
            elif keys and 32<=key<127 and chr(key) in '0123456789.-+eE':draft[keys[index]]+=chr(key)

    @modal_screen
    def form(self,data,design=False,editing=False,grouped=False):
        self.model_creation=design
        draft=dict(data)
        criteria=draft.pop('criteria',{})
        if not design:draft.update(criteria)
        draft.pop('schema_version',None)
        keys=list(draft);values={key:json.dumps(value) if isinstance(value,(list,dict)) else str(value) for key,value in draft.items()}
        field=0;error='';section=0
        while True:
            controls=('Tab / Shift+Tab field    Ctrl+N/P groups    F2 editor    Space choices    Enter review    Esc cancel' if grouped and not design else
                'Tab / Shift+Tab field    F2 editor    Space choices    Enter review    Esc cancel')
            h,w=self.page('MODEL BUILDER' if design else 'EDIT EXPERIMENT' if editing else 'NEW EXPERIMENT',controls)
            if w<64 or h<20:
                self.put(6,4,'Enlarge terminal to at least 64 columns and 20 rows',4)
                if self.wait()==27:return None
                continue
            keys=[key for key in draft if design or key not in inactive_fields(values['kind'])]
            if grouped and not design:
                from .experiment_workspace import FORM_SECTIONS
                group_name,group_keys=FORM_SECTIONS[section]
                keys=[key for key in keys if key in group_keys]
                self.put(3,3,' '*(w-7),limit=w-7)
                x=3
                for i,(name,_) in enumerate(FORM_SECTIONS):
                    label='Reward / validation' if i==3 and w>=100 else 'Reward' if i==3 else name
                    self.put(3,x,label,9 if i==section else 6,i==section,limit=w-x-3);x+=len(label)+3
                self.put(4,3,f'{group_name.upper()} / {section+1}/{len(FORM_SECTIONS)} / Ctrl+N next / Ctrl+P previous',2,limit=w-7)
            field=min(field,len(keys)-1)
            layout=self.draw_inputs(keys,values,field)
            if layout['preview_x'] is not None:self.network_preview(values,layout['preview_x'])
            keyname=keys[field]
            choices=CAPABILITIES['comparisons_by_kind'][values['kind']] if keyname=='comparison' and not design else OPTIONS.get(keyname)
            hint=('Space / F2: select additional trained actors / source checkpoint is the control' if keyname=='values' and values.get('comparison')=='source' else
                'Type 64 64 64 or [64,64,64] / F2 opens the layer editor' if keyname in ('actor_hidden','critic_hidden') else
                'F2 / Space: edit labeled reward parameters / experiments only' if keyname=='reward_parameters' else
                'F2 / Space: choose saved checkpoint; or type an absolute path' if keyname=='source' else
                'Supported: '+', '.join(choices) if choices else
                'Lists: 64 64 64 / 64,64,64 / [64,64,64] / nested: [[64],[128]]'
                if isinstance(draft[keyname],(list,dict)) else
                'Append text / Backspace deletes / Ctrl+U clears / arrows do not edit text')
            self.put(h-6,4,hint,6);self.put(h-1,3,error,7)
            key=self.wait()
            if key==27:return None
            if grouped and not design and key in (curses.KEY_F3,curses.KEY_F4,14,16):
                section=(section+(1 if key in (curses.KEY_F3,14) else -1))%len(FORM_SECTIONS);field=0;continue
            if key in (9,curses.KEY_BTAB,curses.KEY_UP,curses.KEY_DOWN,curses.KEY_NPAGE,curses.KEY_PPAGE) and isinstance(draft[keyname],list):
                try:values[keyname]=json.dumps(parse_list(values[keyname]))
                except ValueError:pass
            if key==curses.KEY_NPAGE:field=min(len(keys)-1,(field//layout['capacity']+1)*layout['capacity'])
            elif key==curses.KEY_PPAGE:field=max(0,(field//layout['capacity']-1)*layout['capacity'])
            elif key in (9,curses.KEY_DOWN):field=(field+1)%len(keys)
            elif key in (curses.KEY_BTAB,curses.KEY_UP):field=(field-1)%len(keys)
            elif key==21:values[keyname]=''
            elif key in (curses.KEY_BACKSPACE,127,8):values[keyname]=values[keyname][:-1]
            elif key==curses.KEY_F2 and keyname in ('actor_hidden','critic_hidden'):
                try:
                    hidden=parse_list(values[keyname]);NetworkDesign(hidden)
                except (ValueError,TypeError):
                    error='Use 64 64 or [64,64], or clear the field and press F2 to add layers';continue
                result=self.numeric_editor(LABELS[keyname],{str(i):value for i,value in enumerate(hidden)},True,values,keyname)
                if result is not None:values[keyname]=json.dumps(result)
            elif key in (32,curses.KEY_F2) and keyname=='values' and values.get('comparison')=='source':
                try:chosen=parse_list(values[keyname])
                except ValueError:chosen=[]
                picked=self.checkpoint_picker(multiple=True,selected=chosen,exclude=values.get('source',''))
                if picked is not None:values[keyname]=json.dumps(picked)
            elif key in (32,curses.KEY_F2) and keyname=='reward_parameters':
                defaults={name:value for name,value in CAPABILITIES['reward_parameters'].items() if name!='angular_dynamic_fraction'}
                try:
                    existing=json.loads(values[keyname] or '{}')
                    if not isinstance(existing,dict):raise ValueError()
                    defaults.update(existing)
                except (ValueError,TypeError):
                    error='Reward overrides must be a named parameter object; clear and press Space to edit';continue
                result=self.numeric_editor('EXPERIMENTAL REWARD PARAMETERS',defaults,preview_values=values,target=keyname)
                if result is not None:values[keyname]=json.dumps({name:value for name,value in result.items() if value!=CAPABILITIES['reward_parameters'][name]})
            elif key in (32,curses.KEY_F2) and keyname=='source':
                source=self.checkpoint_picker()
                if source:
                    import torch
                    saved=torch.load(source,map_location='cpu',weights_only=True)
                    values['source']=str(source)
                    values['initialization_mode']='transfer'
                    if values.get('kind')=='baseline':values['training_seeds']='[42]'
                    values['actor_hidden']=json.dumps([v['outputs'] for v in saved.get('layers',[]) if v['type']=='Linear'][:-1])
                    hidden=[v['type'] for v in saved.get('layers',[]) if v['type']!='Linear'][:-1]
                    if hidden:values['actor_activation']=hidden[0]
                    error='Source picked / actor architecture loaded / source remains read-only'
            elif key==32 and keyname in OPTIONS:
                options=choices;values[keyname]=options[(options.index(values[keyname])+1)%len(options)] if values[keyname] in options else options[0]
            elif key in (10,13):
                try:
                    result={}
                    for name,value in values.items():
                        old=draft[name]
                        result[name]=(parse_list(value) if isinstance(old,list) else json.loads(value) if isinstance(old,dict) else int(value) if type(old) is int else float(value) if type(old) is float else value)
                    if design:
                        return template(result['name'],NetworkDesign(result['actor_hidden'],result['actor_activation'],result['initialization']),NetworkDesign(result['critic_hidden'],result['critic_activation'],result['initialization']))
                    result['criteria']={key:result.pop(key) for key in criteria};result['schema_version']=1
                    return validate(result)
                except (ValueError,TypeError,KeyError) as exc:error=str(exc)
            elif 32<=key<127 and len(values[keyname])<2048:values[keyname]+=chr(key)

    def new_template(self,data=None):
        original=data or template('balance_design')
        flat=dict(name=original['name']+('-copy' if data else ''),actor_hidden=list(original['actor']['hidden_sizes']),critic_hidden=list(original['critic']['hidden_sizes']),
            actor_activation=original['actor']['activation'],critic_activation=original['critic']['activation'],initialization=original['actor']['initialization'])
        result=self.form(flat,True)
        if result and self.confirm('SAVE UNTRAINED DESIGN',[result['name'],'205 inputs / 32 actions / output semantics retained',
            'Actor: '+str(result['actor']),'Critic: '+str(result['critic']),'Configuration only; no learned weights']):
            self.message='Saved '+str(save_template(result,self.templates))

    def new_experiment(self,data=None,design=None,directory=None):
        initial=configuration() if data is None else dict(data,name=data['name']+'-copy')
        if design:
            initial.update(name=design['name']+'-experiment',kind='training',actor_hidden=design['actor']['hidden_sizes'],
                critic_hidden=design['critic']['hidden_sizes'],actor_activation=design['actor']['activation'],critic_activation=design['critic']['activation'],initialization=design['actor']['initialization'])
        result=self.form(initial,grouped=True)
        if result:
            lines=self.review_lines(result)
            if self.confirm('SAVE EXPERIMENT DRAFT',lines):
                path=create_experiment(result,self.directory if directory is None else directory);self.message='Saved draft '+path.name;self.tab='experiments'
                return path

    @modal_screen
    def rename_experiment(self,row):
        name=row['name'];error=''
        while True:
            h,w=self.page('RENAME EXPERIMENT','[Enter] Save    Ctrl+U clear    [Esc] Cancel')
            self.put(6,4,'Name',6)
            self.put(8,4,name+'▏',1,True,limit=w-8)
            self.put(11,4,'Only the name changes. Settings and results stay attached.',6,limit=w-8)
            self.put(h-5,4,error,7,limit=w-8)
            key=self.wait()
            if key==27:return
            if key in (10,13):
                try:rename_experiment(row['path'],name)
                except (ValueError,OSError) as exc:error=str(exc);continue
                self.message='Renamed experiment / settings and results preserved'
                return
            elif key==21:name=''
            elif key in (curses.KEY_BACKSPACE,127,8):name=name[:-1]
            elif 32<=key<=126 and len(name)<64:name+=chr(key)

    def edit_experiment(self,row):
        if row['status']!='draft':
            self.new_experiment(row['configuration'])
            return
        result=self.form(row['configuration'],editing=True,grouped=True)
        if result and self.confirm('SAVE EXPERIMENT CHANGES',self.review_lines(result)):
            update_experiment(row['path'],result)
            self.message='Saved experiment settings'

    def review_lines(self,c):
        warning=worker_warning(max([c['workers']]+(c['values'] if c['comparison']=='workers' else [])))
        learning=c['kind'] in ('training','critic_fit')
        transferred=bool(c['source']) and c['initialization_mode']=='transfer'
        return [c['name']+' / '+c['kind'],warning or 'Independent worlds / fixed policy within each collection batch',
            'Source: '+(c['source'] or 'fresh initialization'),
            'Weights: '+c['initialization_mode']+(' / fresh critic and optimizers' if learning else ' / frozen actor / no optimizer updates'),
            'Actor: '+str(c['actor_hidden'])+' '+c['actor_activation'],
            'Critic: '+str(c['critic_hidden'])+' '+c['critic_activation'],
            'Comparison: '+('Control only' if c['comparison']=='none' else 'trained actors / '+json.dumps(c['values']) if c['comparison']=='source' else c['comparison']+' / '+json.dumps(c['values'])),
            (f'Training budget per branch: {c["episodes"]} episodes/update × {c["updates"]} updates × {len(c["training_seeds"])} seeds' if learning else f'Frozen study / 0 learning updates / {len(c["training_seeds"])} '+('rollout seed groups' if transferred else 'initialization seeds')),
            f'Evaluation: 1 deterministic + {len(c["evaluation_seeds"])} stochastic episodes'+(' before/after learning' if learning else ' / frozen policy'),
            f'Workers: {c["workers"]} / {capabilities()["hardware"]["cpu_cores"]} CPU cores / learner {c["device"]}',
            ('Method: '+c['algorithm']+f' / actor LR {c["actor_lr"]} / critic LR {c["critic_lr"]}' if c['kind']=='training' else f'Fresh critic / actor frozen / critic LR {c["critic_lr"]} / {c["critic_fits"]} fits per batch' if c['kind']=='critic_fit' else 'Frozen actor / no optimizer updates'),
            'Reward: '+c['reward_formula']+(' / '+json.dumps(c['reward_parameters']) if c['reward_parameters'] else ' / default parameters'),
            ('Rollout seeds: ' if transferred and not learning else 'Training seeds: ')+str(c['training_seeds']), 'Evaluation seeds: '+str(c['evaluation_seeds']),
            'Fixed: 205 inputs / 32 actions / neutral reset / existing physics and FALLEN termination',
            f'Physical criteria: balanced ≥ {c["criteria"]["min_balanced_seconds"]:.2f}s / falls ≤ {c["criteria"]["max_fall_rate"]:.0%} / at least {c["criteria"]["minimum_seeds"]} training seeds', 'Outputs: '+str(self.directory)]

    def metric_cards(self,metrics,y,width):
        h,_=self.screen.getmaxyx()
        specs=[('Balanced time','balanced_seconds','s',3),('Balance quality','average_balance_quality','',2),
               ('Balanced fraction','balanced_fraction','%',3),('Fall rate','fall_rate','%',4),
               ('Longest balanced interval','longest_balanced_steps','steps',1),('Recoveries / mean','recoveries','',1),
               ('Episode return','total_reward','',3),('Episode length / mean','steps','steps',1)]
        columns=4 if width>=100 else 2;count=min(8,columns*max(1,(h-y-12)//5));cell=width//columns
        for i,(label,key,unit,color) in enumerate(specs[:count]):
            x=3+(i%columns)*cell;top=y+(i//columns)*5;size=cell-2
            self.put(top,x,'┌'+'─'*(size-2)+'┐',6,limit=size)
            for dy in (1,2,3):
                self.put(top+dy,x,'│',6);self.put(top+dy,x+size-1,'│',6)
            self.put(top+4,x,'└'+'─'*(size-2)+'┘',6,limit=size)
            self.put(top+1,x+2,label,6,limit=size-4)
            value=metrics.get(key)
            shown='Pending' if value is None else (f'{value*100:.1f}%' if unit=='%' else f'{value:.2f}'+(' '+unit if unit else ''))
            self.put(top+2,x+2,shown,color if value is not None else 6,True,size-4)
            if value is not None and key in ('average_balance_quality','balanced_fraction','fall_rate'):
                track=max(1,size-4);fill=round(track*min(1,max(0,value)))
                self.put(top+3,x+2,'─'*track,6,limit=track)
                self.put(top+3,x+2,'━'*fill,color,limit=track)
        return y+((count+columns-1)//columns)*5

    def result_table(self,rows,y,width,offset=0,live=False):
        h,_=self.screen.getmaxyx()
        bottom=getattr(self,'content_bottom',h-5)
        if y+3>=bottom-1:return
        self.put(y,3,'RECENT EVALUATIONS' if live else 'RESULTS BY BRANCH / TRAINING SEED',1,True,width)
        wide=width>=90;name_width=max(18,width-(57 if wide else 30))
        heading=f'{"Branch / seed":<{name_width}}'+('  Balanced / s       Quality    Falls     Return' if wide and live else '  Before → after / s    Quality    Falls     Return' if wide else '  Balanced/s   Quality   Falls')
        self.put(y+2,3,heading,6,limit=width)
        capacity=max(1,bottom-y-4);first=min(offset,max(0,len(rows)-capacity))
        for i,row in enumerate(rows[first:first+capacity]):
            m=row['final']['mean'];before=None if live else row['initial']['mean'];label=row.get('label',f'{row["branch"]} / {row["seed"]}')
            self.put(y+3+i,3,label[:name_width],1,limit=name_width)
            balance=(f'{m["balanced_seconds"]:16.3f}' if wide and live else
                f'{before["balanced_seconds"]:6.3f} → {m["balanced_seconds"]:6.3f}' if wide else f'{m["balanced_seconds"]:9.3f}')
            balance_color=1 if live else 3 if m['balanced_seconds']>before['balanced_seconds'] else 4 if m['balanced_seconds']<before['balanced_seconds'] else 6
            cells=[(balance,balance_color),(f'{m["average_balance_quality"]:8.3f}',2),(f'{m["fall_rate"]:6.0%}',4 if m['fall_rate'] else 3)]
            if wide:cells.append((f'{m["total_reward"]:9.2f}',1))
            x=5+name_width
            for text,color in cells:
                self.put(y+3+i,x,text,color,limit=max(0,3+width-x));x+=len(text)+2
        if not rows:self.put(y+3,3,'Waiting for the first completed evaluation.',6,limit=width)
        if len(rows)>capacity:self.put(bottom-1,3,f'Rows {first+1}–{min(len(rows),first+capacity)} of {len(rows)} / Up/Down scroll',6,limit=width)

    def experiment_overview(self,config,metrics,rows,*,phase,status,elapsed=0,percent=None,remaining=None,offset=0,live=False,mode='Stochastic',branch=''):
        running_keys='[P] resume    [N] resume    [Q] cancel' if status=='paused' else '[P] pause    [Q] cancel    [N] continue'
        h,w=self.page('EXPERIMENT / '+config['name'],(running_keys if live else '[1] Overview    [2] Settings    [3] Comparisons    [4] Benchmarks    [Tab] Branch    [M] Evaluation    Up/Down rows    [Q] Back'))
        sidebar=w>=150;area=w-49 if sidebar else w-6
        self.put(5,3,('PAUSED / ' if status=='paused' else '')+phase,4 if status in ('paused','cancelled','failed') else 1,True,area)
        y=7
        if percent is not None:
            track=max(8,area-30);fill=round(track*min(100,max(0,percent))/100)
            self.put(y,3,'─'*track,6,limit=track);self.put(y,3,'━'*fill,3,limit=track)
            self.put(y,track+5,f'{percent:.1f}% / {elapsed:.0f}s elapsed',3,limit=27)
            self.put(y+1,3,f'Estimated remaining: {remaining:.0f}s' if remaining is not None else 'Estimating after the first completed evaluation',6,limit=area)
            y+=3
        self.put(y,3,mode.upper()+' EVALUATION / '+(branch+' / ' if branch else '')+('mean across evaluation seeds' if mode=='Stochastic' else 'no exploration'),6,limit=area)
        y=self.metric_cards(metrics,y+1,area)
        if h<30 and area<100:
            fraction=metrics.get('balanced_fraction');falls=metrics.get('fall_rate')
            self.put(y,3,'Balanced fraction: '+(f'{fraction:.1%}' if fraction is not None else 'Pending')+' / falls: '+(f'{falls:.0%}' if falls is not None else 'Pending'),6,limit=area)
            y+=1
        self.put(y+1,3,('Frozen baseline / no learning updates' if config['kind']=='baseline' else
            'Actor frozen / critic fitting' if config['kind']=='critic_fit' else
            f'{config["kind"]} / {config["episodes"]} episodes per update / {config["updates"]} updates'),6,limit=area)
        self.result_table(rows,y+3,area,offset,live)
        if sidebar:
            values={key:json.dumps(value) if isinstance(value,(list,dict)) else str(value) for key,value in config.items()}
            self.network_preview(values,w-43,7)
        return h,w

    def run(self,row,confirmed=False):
        if not confirmed and not self.confirm('RUN BOUNDED EXPERIMENT',self.review_lines(row['configuration'])+[row['path']]):return
        started=time.monotonic();phase='Preparing independent workers';metrics={};cancel=False;paused=False;completed=0
        history=[];last_render=0.
        paused_since=None;paused_seconds=0.
        config=row['configuration']
        branch_configs=dict(experiment_branches(config))
        total=sum(len(c['training_seeds'])*(c['updates'] if c['kind'] in ('training','critic_fit') else 1) for c in branch_configs.values())
        def pause(value):
            nonlocal paused,paused_since,paused_seconds,last_render
            if value==paused:return
            now=time.monotonic()
            if value:paused_since=now
            elif paused_since is not None:
                paused_seconds+=now-paused_since;paused_since=None
            paused=value;last_render=0.
            env=getattr(service,'environment',None)
            if env:env.set_paused(value)
        def service():
            nonlocal cancel,last_render
            while True:
                now=time.monotonic()
                elapsed=now-started-paused_seconds-(now-paused_since if paused_since is not None else 0)
                percent=min(100,100*completed/max(1,total));remaining=elapsed*max(0,total-completed)/completed if completed else None
                now=time.monotonic()
                if now-last_render>=.1:
                    h,w=self.experiment_overview(branch_configs.get(phase.split(' / seed ')[0],config),metrics,history,phase=phase,status='paused' if paused else 'running',
                        elapsed=elapsed,percent=percent,remaining=remaining,offset=max(0,len(history)-4),live=True)
                    if cancel:self.put(h-5,4,'Cancel requested. Q again confirms; N continues.',4,True)
                    elif paused:self.put(h-5,4,'Paused / P or N resumes / Q twice cancels.',4,True)
                    self.screen.refresh();last_render=now
                self.screen.timeout(100 if paused else 0);key=self.screen.getch()
                if key in (ord('q'),ord('Q'),27):
                    if cancel:raise KeyboardInterrupt
                    cancel=True;last_render=0.
                elif key in (ord('n'),ord('N')):
                    cancel=False;last_render=0.;pause(False)
                elif key in (ord('p'),ord('P')):
                    pause(not paused)
                if not paused:return
        def progress(text,values):
            nonlocal phase,metrics,completed,last_render
            phase=text;metrics=values;last_render=0.
            if ' / update ' in text or config['kind'] not in ('training','critic_fit'):completed+=1
            history.append(dict(branch=text.split(' / seed ')[0],seed=text.split(' / seed ')[-1].split(' / ')[0],
                label=text,final=dict(mean=values)))
            del history[:-max(16,self.screen.getmaxyx()[0]*2)]
            service()
        def stage(text):
            nonlocal phase,last_render,metrics
            if text.split(' / ')[:2]!=phase.split(' / ')[:2]:metrics={}
            phase=text;last_render=0.
            service()
        try:
            run_experiment(row['path'],progress=progress,service=service,stage=stage)
            self.message='Completed / results saved / promotion requires explicit criteria review'
        except KeyboardInterrupt:self.message='Cancelled / completed evidence preserved'
        except Exception as exc:
            detail=' / details in error.txt' if (Path(row['path'])/'error.txt').is_file() else ''
            self.set_message('Failed: '+str(exc)+detail,error=True)
        finally:self.screen.timeout(-1)

    def details(self,row,benchmark=False):
        from .experiment_workspace import workspace
        workspace(self,row,benchmark)

    def loop(self):
        while True:
            designs=self.tab=='templates'
            records=[]
            if designs:
                for path in sorted(self.templates.glob('*.json')):
                    if path.name=='default.json':continue
                    try:data=load_template(path);records.append(dict(path=str(path),name=data['name'],data=data))
                    except (ValueError,OSError,KeyError):pass
            else:records=library(self.directory)
            self.selection=min(self.selection,max(0,len(records)-1))
            h,w=self.page('MODEL DESIGNS' if designs else 'EXPERIMENT LIBRARY')
            if w<64 or h<20:self.put(6,4,'Enlarge terminal to at least 64 columns and 20 rows',4)
            elif not records:self.put(6,4,'No saved designs yet. N creates one.' if designs else 'No experiments yet. N opens labeled settings.',6)
            row=records[self.selection] if records else None
            actions=[('[N] New',True),('[Enter] Inspect',row is not None),('[C] Copy',bool(row and (designs or row.get('configuration'))))]
            if not designs:actions.append(('[E] Edit' if row and row['status']=='draft' else '[E] Edit copy',bool(row and row.get('configuration') and row['status']!='running')))
            if not designs:actions.append(('[B] Benchmarks',bool(row and row.get('configuration'))))
            if not designs:actions.append(('[F2] Rename',bool(row and row.get('configuration') and row['status']!='running')))
            actions+=([('[T] Experiment',row is not None)] if designs else [('[R] Run draft',bool(row and row['status']=='draft')),('[P] Promote',bool(row and row['status']=='completed'))])
            root=self.templates.parent if designs else self.directory.parent
            can_restore=any((item/'item').exists() for item in (root/'.trash').glob('*'))
            actions.extend([('[U] Restore',can_restore),('[Q] Back',True),('[D] Delete',row is not None)])
            self.action_footer(actions)
            if records and w>=64 and h>=20:
                self.rows([('Untrained / '+r['name']+' / '+str(r['data']['actor']['hidden_sizes'])) if designs else r['status']+' / '+r['kind']+' / '+r['name'] for r in records],self.selection)
            key=self.wait()
            try:
                if key in (ord('q'),ord('Q'),27):return
                if key in (ord('i'),ord('I')):self.notice('SUPPORTED EXPERIMENTS',[str(k)+': '+str(v) for k,v in capabilities().items()])
                elif key==curses.KEY_DOWN:self.selection=min(max(0,len(records)-1),self.selection+1)
                elif key==curses.KEY_UP:self.selection=max(0,self.selection-1)
                elif key in (ord('n'),ord('N')):
                    if designs:self.new_template()
                    else:
                        created=self.new_experiment()
                        if created:
                            from .experiment_workspace import read_record
                            self.details(read_record(created)[0])
                elif row and key in (10,13):self.notice('UNTRAINED MODEL DESIGN',[row['name'],'Actor: '+str(row['data']['actor']),'Critic: '+str(row['data']['critic']),'205 inputs / 32 actions / no learned weights']) if designs else self.details(row)
                elif row and not designs and row.get('configuration') and key==curses.KEY_F2:self.rename_experiment(row)
                elif row and not designs and key in (ord('b'),ord('B')):self.details(row,benchmark=True)
                elif row and key in (ord('c'),ord('C')):self.new_template(row['data']) if designs else self.new_experiment(row['configuration']) if row.get('configuration') else self.notice('INVALID RECORD',['Cannot duplicate an unreadable configuration; original files preserved'])
                elif row and not designs and row.get('configuration') and key in (ord('e'),ord('E')):
                    if row['status']=='running':self.set_message('Stop the experiment before editing its settings',error=True)
                    else:self.edit_experiment(row)
                elif row and key in (ord('d'),ord('D')):
                    if self.confirm('DELETE SAVED ITEM',[row['name'],'Move this item to recoverable trash?',
                        'Only this model design moves.' if designs else 'This experiment moves with its output checkpoints and evidence.','U restores the last deleted item.']):
                        trash(row['path'],root);self.message='Moved to trash / U restores the last deletion'
                elif key in (ord('u'),ord('U')) and can_restore:
                    restore_last(root);self.message='Restored the last deleted item'
                elif row and designs and key in (ord('t'),ord('T')):self.new_experiment(design=row['data'])
                elif row and not designs and key in (ord('r'),ord('R')) and row['status']=='draft':self.run(row)
                elif row and not designs and key in (ord('p'),ord('P')) and row['status']=='completed':
                    results=json.loads((Path(row['path'])/'results.json').read_text())
                    candidates=list(dict.fromkeys(item['branch'] for item in results['rows']))
                    choice=self.choose('CANDIDATE TO PROMOTE',candidates)
                    if choice is not None and self.confirm('PROMOTE TESTED DESIGN',[
                        candidates[choice]+' / use architecture with fresh weights as future CLI default?',
                        json.dumps(row['configuration']['criteria']),
                        'Production reward stays unchanged. Every seed must pass.',
                        'This does not validate unseen postures or promote trained weights.']):
                        self.message='Promoted '+str(promote(row['path'],candidates[choice],directory=self.templates))
            except Exception as exc:self.set_message(str(exc),error=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--create',action='store_true',help='Open the model builder directly, then return to the CLI')
    parser.add_argument('--tab',choices=('templates','experiments'),default='experiments',help='Open the corresponding saved library')
    parser.add_argument('--directory',type=Path,default=LIBRARY);parser.add_argument('--templates',type=Path,default=TEMPLATE_DIR)
    args=parser.parse_args()
    if not sys.stdin.isatty() or not sys.stdout.isatty():parser.error('This library needs an interactive terminal. Open aura-cli and use /experiment.')
    sys.stdout.write('\033[?2004l\033[?1000l\033[?1002l\033[?1006l');sys.stdout.flush()
    try:
        def launch(screen):
            ui=LibraryUI(screen,'templates' if args.create else args.tab,args.directory,args.templates)
            if args.create:ui.new_template()
            else:ui.loop()
        run_screen(launch)
    except KeyboardInterrupt:
        pass


if __name__=='__main__':main()
