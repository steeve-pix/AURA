"""Read-only center catalog and recoverable removal of managed library items."""
import argparse
from contextlib import ExitStack
from datetime import datetime,timezone
import fcntl
import json
import os
from pathlib import Path
from uuid import uuid4

ROOT=Path(__file__).resolve().parents[2]/'models'

def catalog(root=ROOT):
    root=Path(root)
    for kind,paths in (('design',(root/'templates').glob('*.json')),('experiment',(root/'experiments').glob('*'))):
        for path in sorted(paths):
            if path.name=='default.json' or path.name.startswith('.') or path.is_symlink():continue
            if kind=='experiment' and not path.is_dir():continue
            try:
                data=json.loads((path/'configuration.json' if kind=='experiment' else path).read_text())
                state=json.loads((path/'state.json').read_text()) if kind=='experiment' else {}
                name=data['name'];status=state.get('status','untrained design')
                if kind=='design':
                    from python.model_templates import validate_template
                    data=validate_template(data)
                    details=['Untrained design / no learned weights',f"Actor: {data['actor']['hidden_sizes']} / {data['actor']['activation']}",f"Critic: {data['critic']['hidden_sizes']} / {data['critic']['activation']}",f"Initialization: {data['actor']['initialization']}",'205 observations / 32 actions']
                else:
                    details=[f"Study: {data['kind']}",f"Status: {status}",f"Actor: {data['actor_hidden']} / {data['actor_activation']}",f"Critic: {data['critic_hidden']} / {data['critic_activation']}",f"Episodes/update: {data['episodes']} / updates: {data['updates']}",f"Workers: {data['workers']} / device: {data['device']}",f"Reward: {data['reward_formula']}",f"Training seeds: {data['training_seeds']}",f"Evaluation seeds: {data['evaluation_seeds']}"]
                    if state.get('error'):details.append('Error: '+state['error'])
                    results=path/'results.json'
                    if results.exists():
                        for row in json.loads(results.read_text()).get('rows',[]):
                            metric=row['final']['mean'];details.append(f"{row['branch']} / seed {row['seed']}: balanced {metric['balanced_seconds']:.3f}s / return {metric['total_reward']:.2f} / falls {metric['fall_rate']:.2f}")
                    if (path/'promotion.json').exists():status='validated';details.append('Promotion criteria: '+json.dumps(json.loads((path/'promotion.json').read_text())['criteria']))
                details+=['Location: '+str(path),'Open /experiment to configure or run a study.' if kind=='experiment' else 'Open /create to create a new design.']
            except (OSError,ValueError,KeyError,TypeError) as error:
                name=path.stem;status='unreadable';details=['Cannot read saved metadata: '+str(error),'Files remain available for recovery.']
            yield dict(path=str(path.resolve()),kind=kind,name=name,status=status,details=details)

def catalog_lines(root=ROOT):
    for row in catalog(root):
        encode=lambda value:str(value).encode().hex()
        yield '\t'.join(['AURA_SAVED',encode(row['path']),row['kind'],encode(row['name']),encode(row['status'])])
        for detail in row['details']:yield '\t'.join(['AURA_SAVED_DETAIL',encode(row['path']),encode(detail)])

def managed_path(path,root):
    root=Path(root).resolve();original=Path(path).absolute();resolved=original.resolve()
    if original.is_symlink() or resolved==root or not resolved.is_relative_to(root):raise ValueError('Choose a saved item inside the model library')
    relative=resolved.relative_to(root);parts=relative.parts
    checkpoint=resolved.is_file() and resolved.suffix=='.pt' and (len(parts)==1 or parts[0] in ('critics','experiments')) and resolved.name!='source_snapshot.pt'
    design=resolved.is_file() and len(parts)==2 and parts[0]=='templates' and resolved.suffix=='.json' and resolved.name!='default.json'
    experiment=resolved.is_dir() and len(parts)==2 and parts[0]=='experiments' and not resolved.name.startswith('.')
    if not (checkpoint or design or experiment):raise ValueError('This path is not a deletable saved item')
    return resolved

def trash(path,root=ROOT):
    root=Path(root).resolve();path=managed_path(path,root)
    default=root/'templates/default.json'
    if default.exists() and path.parent==root/'templates':
        if Path(json.loads(default.read_text())['template']).resolve()==path:raise ValueError('This is the default model design; choose another default before deleting it')
    with ExitStack() as locks:
        lock_paths=[path/'.run.lock'] if path.is_dir() else [path.parent/('.'+path.name+'.lock')]
        if path.is_relative_to(root/'experiments'):
            relative=path.relative_to(root/'experiments');lock_paths.append(root/'experiments'/relative.parts[0]/'.run.lock')
        for lock_path in dict.fromkeys(lock_paths):
            stream=locks.enter_context(lock_path.open('a'))
            try:fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError as error:raise ValueError('Item is in use; stop its training or experiment before deleting it') from error
        destination=root/'.trash'/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%f')+'-'+uuid4().hex[:8])
        destination.mkdir(parents=True)
        (destination/'metadata.json').write_text(json.dumps(dict(original=str(path.relative_to(root)),deleted_utc=datetime.now(timezone.utc).isoformat()),indent=2))
        os.rename(path,destination/'item')
    return destination

def restore_last(root=ROOT):
    root=Path(root).resolve()
    for folder in sorted((root/'.trash').glob('*'),reverse=True):
        item=folder/'item'
        if not item.exists():continue
        metadata=json.loads((folder/'metadata.json').read_text());target=(root/metadata['original']).resolve()
        if not target.is_relative_to(root) or target==root:raise ValueError('Invalid recovery destination')
        if target.exists():raise ValueError('An item already exists at the original location; nothing was overwritten')
        target.parent.mkdir(parents=True,exist_ok=True);os.rename(item,target)
        return target
    raise ValueError('No deleted item is available to restore')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('action',choices=('catalog','delete','restore'));parser.add_argument('path',nargs='?');args=parser.parse_args()
    try:
        if args.action=='catalog':print('\n'.join(catalog_lines()))
        elif args.action=='delete':print('Moved to recoverable trash: '+str(trash(args.path)))
        else:print('Restored: '+str(restore_last()))
    except (OSError,ValueError,TypeError) as error:parser.exit(1,str(error)+'\n')
if __name__=='__main__':main()
