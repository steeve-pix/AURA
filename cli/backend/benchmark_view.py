"""Measured experiment results rendered as grouped terminal bar charts."""
import math
import textwrap
from pathlib import Path
from statistics import mean


METRICS=(
    ('Balanced time','balanced_seconds','s',True),
    ('Balanced fraction','balanced_fraction','%',True),
    ('Balance quality','average_balance_quality','',True),
    ('Fall rate','fall_rate','%',False),
    ('Episode return','total_reward','',True),
    ('Collection throughput','physics_steps_per_second','steps/s',True),
    ('Episode throughput','episodes_per_second','episodes/s',True),
    ('Collection time','collection_seconds','s',False),
    ('Posture','component:posture','',True),
    ('Height','component:height','',True),
    ('COM motion','component:com_motion','',True),
    ('Body rotation','component:body_rotation','',True),
    ('Support','component:support','',True),
    ('Stationary hold','component:stationary_hold','',True),
    ('Foot grip','component:foot_grip','',True),
)
COLORS=(2,3,5,4)


def benchmark_groups(rows,deterministic=False):
    """Aggregate each branch equally across its recorded training seeds."""
    branches=list(dict.fromkeys(row['branch'] for row in rows))
    phases=('initial','final') if len(branches)==1 and any(row.get('updates') for row in rows) else ('final',)
    mode='deterministic' if deterministic else 'mean'
    groups=[]
    for title,key,unit,higher in METRICS:
        series=[]
        for index,branch in enumerate(branches):
            selected=[row for row in rows if row['branch']==branch]
            for phase in phases:
                values=[]
                for row in selected:
                    value=(row.get('mean_components',{}).get(key.split(':',1)[1]) if key.startswith('component:') else
                        row.get(key) if key in ('physics_steps_per_second','episodes_per_second','collection_seconds') else row.get(phase,{}).get(mode,{}).get(key))
                    if isinstance(value,(int,float)) and math.isfinite(value):values.append(value)
                # Collection timing has no before/after observations.
                if not values or (len(phases)>1 and phase=='initial' and (key.startswith('component:') or key in ('physics_steps_per_second','episodes_per_second','collection_seconds'))):continue
                label=branch+(' / before' if phase=='initial' else ' / after' if len(phases)>1 else '')
                if selected[0].get('source_checkpoint'):
                    label=f'A{index+1} / '+Path(selected[0]['source_checkpoint']).stem
                series.append(dict(label=label,value=mean(values),count=len(values),
                    low=min(values),high=max(values),color=COLORS[(index if len(phases)==1 else phases.index(phase))%len(COLORS)]))
        if series:groups.append(dict(title=title,key=key,unit=unit,higher=higher,series=series))
    # Reward audits put their component measurements first, with one actor per color.
    return sorted(groups,key=lambda group:not group['key'].startswith('component:'))


def format_value(value,unit):
    return f'{value*100:.1f}%' if unit=='%' else f'{value:.2f}'+(' '+unit if unit else '')


def draw_benchmarks(ui,rows,name,scroll=0,deterministic=False,status='completed',components_only=False):
    hint='[1] Overview    [2] Settings    [3] Comparisons    [4] Benchmarks    [M] Evaluation    Up/Down metrics    [Q] Back'
    h,w=ui.page('BENCHMARKS / '+name,hint)
    groups=benchmark_groups(rows,deterministic)
    if components_only:groups=[g for g in groups if g['key'].startswith('component:')]
    ui.put(5,3,('DETERMINISTIC' if deterministic else 'STOCHASTIC')+' / '+status.upper()+' / measured branch means',1,True,limit=w-7)
    components=any(row.get('mean_components') for row in rows)
    ui.put(6,3,('Reward component means / higher quality is better / matched rollout groups' if components_only else
        'Reward components: stochastic collection / M changes evaluation metrics only' if components else
        'Independent metric scales / arrows show better direction / ranges span rollout seeds'),6,limit=w-7)
    if w<64 or h<24:
        ui.put(8,3,'Enlarge terminal to at least 64 columns and 24 rows.',4,limit=w-7)
        return 0
    if not groups:
        ui.put(9,3,'No measurements yet. Run this draft to populate the benchmark charts.',6,limit=w-7)
        return 0
    footer_hint=hint if w>=100 else hint.replace('    ','  ')
    bottom=h-3-len(textwrap.wrap(footer_hint,max(1,w-7)))
    capacity=max(1,bottom-11)
    # Split large branch groups so every series remains reachable on short screens.
    groups=[dict(group,series=group['series'][i:i+capacity],title=group['title']+(' / continued' if i else ''))
            for group in groups for i in range(0,len(group['series']),capacity)]
    scroll=min(max(0,scroll),len(groups)-1)
    y=8;shown=0
    columns=2 if components_only and w>=150 else 1
    cell_width=(w-6)//columns if columns>1 else w
    visible=groups[scroll:]
    for offset,group in enumerate(visible):
        local_width=cell_width;left=3+(offset%columns)*cell_width
        # Keep complete metric groups together, except on terminals too short for one group.
        needed=max(len(g['series']) for g in visible[(offset//columns)*columns:(offset//columns+1)*columns])+4
        if offset%columns==0 and shown and y+needed>bottom:break
        ui.put(y,left,group['title']+' / '+('↑ higher is better' if group['higher'] else '↓ lower is better'),1,True,limit=local_width-7)
        maximum=max(item['value'] for item in group['series'])
        minimum=min(0.,min(item['value'] for item in group['series']))
        scale=1. if group['unit']=='%' or group['key']=='average_balance_quality' or group['key'].startswith('component:') else maximum*1.05 if maximum>0 else 1.
        label_width=22 if columns>1 else min(26,max(14,(local_width-8)//5))
        right_width=52 if local_width>=150 else 28 if local_width>=110 else 14
        start=left+1+label_width;bar_width=max(4,local_width-label_width-right_width-9)
        ui.put(y+1,start,format_value(minimum,group['unit']),6,limit=bar_width)
        axis=format_value(scale,group['unit']);ui.put(y+1,start+max(0,bar_width-len(axis)),axis,6,limit=bar_width)
        for i,item in enumerate(group['series']):
            line=y+2+i
            if line>=bottom:break
            ui.put(line,left,item['label'],item['color'],limit=label_width)
            ui.put(line,start,'─'*bar_width,6,limit=bar_width)
            origin=round((bar_width-1)*(0-minimum)/(scale-minimum))
            endpoint=round((bar_width-1)*(item['value']-minimum)/(scale-minimum))
            fill=abs(endpoint-origin)
            if fill:ui.put(line,start+min(origin,endpoint),'█'*fill,item['color'],limit=fill)
            if minimum<0:ui.put(line,start+origin,'│',6)
            text=format_value(item['value'],group['unit'])
            if local_width>=70:text+=f' / n={item["count"]}'
            if local_width>=150:
                low=f'{item["low"]*100:.1f}%' if group['unit']=='%' else f'{item["low"]:.2f}'
                high=f'{item["high"]*100:.1f}%' if group['unit']=='%' else f'{item["high"]:.2f}'
                text+=f' / {low}–{high}'
            ui.put(line,start+bar_width+2,text,item['color'],True,limit=right_width)
        shown+=1
        if offset%columns==columns-1 or offset==len(visible)-1:y+=needed
        if y>=bottom:break
    ui.put(bottom,3,f'Chart groups {scroll+1}–{min(len(groups),scroll+shown)} of {len(groups)} / '+
        ('matched actors / common 0–1 component scales' if components_only else 'Compare tab: [4]' if getattr(ui,'workspace_context',None) else 'matched comparisons: [3]' if len({row['branch'] for row in rows})>1 else 'one branch / before and after shown when trained'),6,limit=w-7)
    return scroll
