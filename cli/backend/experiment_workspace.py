"""One workspace over existing experiments and additive, linked runs/audits."""
import curses
import json
import math
from pathlib import Path
from statistics import mean
from python.experiment_library import configuration,create_experiment,library,update_experiment
from python.model_templates import load_template
from .benchmark_view import draw_benchmarks

TABS=('Setup','Training','Audit','Compare','Runs')
FORM_SECTIONS=(
    ('Study',('name','kind','source','initialization_mode','comparison','values')),
    ('Network',('actor_hidden','actor_activation','critic_hidden','critic_activation','initialization')),
    ('Learning',('device','actor_lr','critic_lr','episodes','updates','workers','algorithm','critic_fits')),
    ('Reward / validation',('reward_formula','reward_parameters','training_seeds','evaluation_seeds','starting_condition','min_balanced_seconds','max_fall_rate','minimum_seeds')),
)


def read_record(path):
    path=Path(path)
    config=json.loads((path/'configuration.json').read_text())
    state=json.loads((path/'state.json').read_text())
    rows=json.loads((path/'results.json').read_text()).get('rows',[]) if (path/'results.json').exists() else []
    return dict(path=str(path),name=config['name'],kind=config['kind'],configuration=config,status=state['status']),state,rows


class RecordCache:
    """Reparse large histories only when an atomic evidence file changes."""
    def __init__(self):self.records={}

    def read(self,path):
        path=Path(path)
        signature=[]
        for name in ('configuration.json','state.json','results.json'):
            file=path/name
            try:
                stat=file.stat();signature.append((stat.st_ino,stat.st_mtime_ns,stat.st_size))
            except FileNotFoundError:signature.append(None)
        previous=self.records.get(path)
        if previous is None or previous[0]!=signature:
            previous=(signature,read_record(path));self.records[path]=previous
        return previous[1]


def audit_configuration(path):
    """Select every saved trained actor, using portable paths after restore/move."""
    record,state,rows=read_record(path);path=Path(path)
    if state['status']=='running':raise ValueError('Wait for training to finish before auditing its actors')
    if record['kind']!='training':raise ValueError('This run has no trained actors; select a training run in Runs')
    sources=[]
    for row in rows:
        if not row.get('updates'):continue
        candidate=Path(row.get('checkpoint',''))
        if not candidate.is_file():candidate=path/Path(row.get('checkpoint',f'{row["branch"]}-seed{row["seed"]}.pt')).name
        if not candidate.is_file() or not candidate.resolve().is_relative_to(path.resolve()):
            raise ValueError(f'Missing trained checkpoint: {row["branch"]} / seed {row["seed"]}')
        if str(candidate.resolve()) not in sources:sources.append(str(candidate.resolve()))
    if not sources:raise ValueError('No trained checkpoints saved yet. Run training first.')
    original=record['configuration']
    result=configuration(original['name']+'-audit')
    for key in ('actor_hidden','actor_activation','critic_hidden','critic_activation','initialization','workers','episodes','evaluation_seeds','training_seeds','reward_formula','reward_parameters','criteria'):
        result[key]=original[key]
    result.update(kind='reward_audit',source=sources[0],initialization_mode='transfer',
                  comparison='source' if len(sources)>1 else 'none',values=sources[1:])
    return result


def create_actor_audit(path):
    import fcntl
    path=Path(path)
    with (path/'.run.lock').open('a') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError as exc:raise ValueError('Training is still using these actors') from exc
        return create_experiment(audit_configuration(path),path/'audits')


def linked_records(root):
    root=Path(root);records=[read_record(root)[0]]
    seen={root.resolve()}
    for record in records:
        for directory in ('runs','audits'):
            for child in library(Path(record['path'])/directory):
                path=Path(child['path']).resolve()
                if path not in seen and path.is_relative_to(root.resolve()):
                    records.append(child);seen.add(path)
    return records


def settings(ui,config,section):
    h,w=ui.page('','[Tab] settings group    [E] Edit    [R] Run draft    [Q] Back')
    ui.put(5,3,'SETUP / '+FORM_SECTIONS[section][0].upper(),1,True)
    groups=dict(config,**config['criteria'])
    from .experiment_ui import LABELS
    y=7
    for key in FORM_SECTIONS[section][1]:
        if key not in groups:continue
        value=groups[key]
        if key=='values' and config['comparison']=='source':value=f'{len(value)} additional actors'
        if key=='source':value=Path(value).name if value else 'Fresh initialization'
        text=json.dumps(value) if isinstance(value,(list,dict)) else str(value)
        ui.put(y,3,LABELS[key],6,limit=min(35,w//2))
        value_x=3+min(36,w//2)
        ui.put(y,value_x,text,2 if key.startswith(('actor','critic')) else 1,limit=(w-49 if w>=150 else w-7)-value_x)
        y+=2 if h>=34 else 1
    ui.put(h-5,3,f'Group {section+1}/{len(FORM_SECTIONS)} / Tab switches groups; edit opens the same grouped settings.',6,limit=w-7)
    if w>=150:
        values={key:json.dumps(value) if isinstance(value,(list,dict)) else str(value) for key,value in config.items()}
        ui.network_preview(values,w-43,7)


def history(ui,rows,branch,scroll,deterministic=False):
    h,w=ui.page('','[Tab] Branch    [H] Summary    Up/Down history    [A] Audit actors    [Q] Back')
    ui.put(5,3,'TRAINING HISTORY / '+branch+' / '+('DETERMINISTIC' if deterministic else 'STOCHASTIC'),1,True)
    ui.put(7,3,f'{"Update":<8}{"Seeds":<8}{"Balance/s":<13}{"Falls":<9}{"Critic RMSE":<13}EV',6,limit=w-4)
    grouped={}
    for row in rows:
        if row['branch']!=branch:continue
        for update in row.get('updates',[]):grouped.setdefault(update['update'],[]).append(update)
    records=sorted(grouped.items())
    bottom=getattr(ui,'content_bottom',h-4)
    capacity=max(1,bottom-10);scroll=min(scroll,max(0,len(records)-capacity))
    for i,(index,updates) in enumerate(records[scroll:scroll+capacity]):
        mode='deterministic' if deterministic else 'mean'
        fallback='deterministic' if deterministic else 'stochastic'
        metrics=[u.get('evaluation',{}).get(mode,u.get(fallback,{})) for u in updates]
        critics=[u.get('evaluation',{}).get('critic_accuracy',{}) for u in updates]
        def average(items,key):
            values=[item[key] for item in items if isinstance(item.get(key),(int,float)) and math.isfinite(item[key])]
            return mean(values) if values else None
        balance=average(metrics,'balanced_seconds');falls=average(metrics,'fall_rate')
        rmse=average(critics,'rmse_raw');ev=average(critics,'explained_variance')
        cells=[(f'{index:<8}',1),(f'{len(updates):<8}',6),
            (f'{balance:<13.3f}' if balance is not None else 'unavailable  ',3 if balance is not None else 6),
            (f'{falls:<9.0%}' if falls is not None else 'unknown  ',4 if falls else 3 if falls is not None else 6),
            (f'{rmse:<13.3f}' if rmse is not None else 'unavailable  ',2 if rmse is not None else 6),
            (f'{ev:.3f}' if ev is not None else 'unavailable',3 if ev is not None and ev>0 else 4 if ev is not None else 6)]
        x=3
        for text,color in cells:
            ui.put(9+i,x,text,color,limit=max(0,w-4-x));x+=len(text)
    if not records:ui.put(9,3,'No per-update history saved for this run.',6,limit=w-7)
    ui.put(bottom-1,3,'Seed means / critic uses stochastic evaluation / lower RMSE / explained variance (EV) near 1.',6,limit=w-7)
    return scroll


def workspace(ui,row,benchmark=False):
    root=Path(row['path']);current=root;tab=4 if benchmark else 1
    scroll=0;section=0;branch_index=0;deterministic=False;show_history=False;audit_path=None;cache=RecordCache()
    try:
        while True:
            row,state,rows=cache.read(current);config=row['configuration']
            branches=list(dict.fromkeys(r['branch'] for r in rows));branch_index=min(branch_index,max(0,len(branches)-1))
            branch=branches[branch_index] if branches else ''
            ui.workspace_context=(config['name'],tab)
            audits=library(current/'audits')
            if audit_path is None and audits:
                audit_path=next((Path(r['path']) for r in audits if r['status']!='invalid'),None)
            if tab==1:settings(ui,config,section)
            elif tab==2:
                if show_history:scroll=history(ui,rows,branch,scroll,deterministic)
                else:
                    key='deterministic' if deterministic else 'mean'
                    displayed=[dict(r,initial=dict(mean=r['initial'][key]),final=dict(mean=r['final'][key])) for r in rows if key in r.get('final',{}) and key in r.get('initial',{})]
                    selected=[r for r in displayed if r['branch']==branch]
                    metrics={k:sum(r['final']['mean'][k] for r in selected)/len(selected) for k in selected[0]['final']['mean']} if selected else {}
                    selected_config=next((r.get('configuration',config) for r in rows if r['branch']==branch),config)
                    ui.experiment_overview(selected_config,metrics,displayed,phase=state['status'].upper()+' / '+branch,status=state['status'],offset=scroll,mode='Deterministic' if deterministic else 'Stochastic',branch=branch)
                    if state.get('error'):ui.put(6,3,'Error: '+state['error'],7,limit=ui.screen.getmaxyx()[1]-7)
            elif tab in (3,4):
                target=audit_path if tab==3 else current
                if tab==3 and config['kind']=='reward_audit':target=current
                if target:
                    try:record,record_state,chart_rows=cache.read(target)
                    except (ValueError,OSError,KeyError) as exc:
                        audit_path=None;ui.set_message('Cannot read saved audit: '+str(exc),error=True)
                        ui.page('');ui.put(6,3,'Audit evidence unavailable / files preserved',4)
                        key=ui.wait()
                        if key in (27,ord('q'),ord('Q')):return
                        tab=1;continue
                    scroll=draw_benchmarks(ui,chart_rows,record['name'],scroll,deterministic,record_state['status'],components_only=tab==3)
                    if tab==3:
                        width=ui.screen.getmaxyx()[1]-7
                        ui.put(5,3,' '*width,limit=width)
                        ui.put(5,3,'AUDIT / '+record['name']+' / '+record_state['status'].upper(),1,True,limit=width)
                else:
                    ui.page('','[A] Audit all trained actors    [L] Choose audit    [Q] Back')
                    ui.put(6,3,'REWARD AUDIT / ALL TRAINED ACTORS',1,True)
                    ui.put(8,3,'Press A to audit the saved actors from this training run together.',6)
                    ui.put(10,3,'Matched rollout seeds / frozen weights / grouped reward-component bars',6)
            else:
                records=linked_records(root)
                ui.page('','Up/Down select    [Enter] Open run    [Q] Back')
                ui.put(5,3,'SAVED RUNS / original, edited copies and linked audits',1,True)
                scroll=min(scroll,max(0,len(records)-1))
                ui.rows([('* ' if Path(r['path'])==current else '  ')+f'{r["status"]} / {r["kind"]} / {r["name"]}' for r in records],scroll,7)
            key=ui.wait()
            if key in (27,ord('q'),ord('Q')):return
            if key in (ord('1'),ord('2'),ord('3'),ord('4'),ord('5')):tab=int(chr(key));scroll=0;continue
            try:
                if key==curses.KEY_F2:
                    ui.rename_experiment(row)
                elif key in (ord('a'),ord('A')):
                    audit=audit_configuration(current)
                    ui.workspace_context=None
                    if ui.confirm('AUDIT ALL TRAINED ACTORS',[
                        f'{1+len(audit["values"])} trained actors / {len(audit["training_seeds"])} matched rollout seed groups',
                        f'{audit["episodes"]} episodes per actor per group / reward: {audit["reward_formula"]}',
                        'Frozen snapshots / no weight updates / saved inside this experiment']):
                        audit_path=create_actor_audit(current)
                        audit_row=read_record(audit_path)[0]
                        ui.workspace_context=(config['name'],3)
                        ui.run(audit_row,confirmed=True);tab=3;scroll=0
                elif key in (ord('d'),ord('D')) and tab==1:
                    if row['status']=='running':raise ValueError('Wait for training to finish before changing the design')
                    designs=sorted(p for p in ui.templates.glob('*.json') if p.name!='default.json')
                    if not designs:raise ValueError('No saved designs yet / use /create to save a reusable design')
                    ui.workspace_context=None
                    choice=ui.choose('SAVED MODEL DESIGNS',[p.stem for p in designs])
                    if choice is not None:
                        design=load_template(designs[choice]);changed=dict(config,source='',initialization_mode='fresh')
                        for prefix in ('actor','critic'):
                            changed[prefix+'_hidden']=design[prefix]['hidden_sizes']
                            changed[prefix+'_activation']=design[prefix]['activation']
                        changed['initialization']=design['actor']['initialization']
                        if changed['comparison']=='source':changed.update(comparison='none',values=[])
                        if row['status']!='draft':changed['name']+='-copy'
                        result=ui.form(changed,editing=True,grouped=True)
                        if result and ui.confirm('SAVE DESIGN SETTINGS',ui.review_lines(result)):
                            if row['status']=='draft':update_experiment(current,result)
                            else:current=create_experiment(result,root/'runs');audit_path=None
                            ui.message='Saved network design / fresh weights selected'
                elif key in (ord('e'),ord('E')):
                    ui.workspace_context=None
                    if row['status']=='draft':ui.edit_experiment(row)
                    elif row['status']!='running':
                        new_path=ui.new_experiment(config,directory=root/'runs')
                        if new_path:current=Path(new_path);audit_path=None;tab=1;scroll=0
                elif key in (ord('r'),ord('R')):
                    target=audit_path if tab==3 and audit_path else current
                    target_row=read_record(target)[0]
                    if target_row['status']=='draft':
                        ui.workspace_context=(config['name'],3 if tab==3 else 2)
                        ui.run(target_row)
                        tab=3 if tab==3 else 2;scroll=0
                    else:ui.message='This run already has evidence / E creates an editable copy'
                elif key in (ord('l'),ord('L')) and tab==3:
                    if not audits:
                        ui.message='No saved audits yet / A audits all trained actors'
                        continue
                    index=ui.choose('SAVED AUDITS',[r['name']+' / '+r['status'] for r in audits])
                    if index is not None:
                        candidate=Path(audits[index]['path']);cache.read(candidate)
                        audit_path=candidate;scroll=0
                elif key in (10,13) and tab==5 and records:
                    candidate=Path(records[scroll]['path']);cache.read(candidate)
                    current=candidate;tab=3 if records[scroll]['kind']=='reward_audit' else 2;audit_path=None;scroll=0
                elif key in (9,curses.KEY_BTAB):
                    direction=1 if key==9 else -1
                    if tab==1:section=(section+direction)%len(FORM_SECTIONS)
                    elif tab==2 and branches:branch_index=(branch_index+direction)%len(branches);scroll=0
                elif key in (ord('m'),ord('M')) and tab in (2,4):deterministic=not deterministic
                elif key in (ord('h'),ord('H')) and tab==2:show_history=not show_history;scroll=0
                elif key==curses.KEY_DOWN:scroll+=1
                elif key==curses.KEY_UP:scroll=max(0,scroll-1)
            except Exception as exc:ui.set_message(str(exc),error=True)
    finally:ui.workspace_context=None
