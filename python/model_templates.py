"""Validated network designs, independent of learned checkpoint weights."""
from dataclasses import dataclass, asdict
import json
from pathlib import Path
import re
import os
import tempfile
from torch import nn

ACTIVATIONS = {'Tanh': nn.Tanh, 'ReLU': nn.ReLU, 'Sigmoid': nn.Sigmoid, 'Identity': nn.Identity}
MAX_HIDDEN_LAYERS = 8
MAX_NEURONS = 2048
INITIALIZATIONS = ('default', 'orthogonal', 'xavier_uniform')
TEMPLATE_DIR = Path(__file__).resolve().parents[1] / 'models/templates'


@dataclass(frozen=True)
class NetworkDesign:
    hidden_sizes: tuple = (64,64)
    activation: str = 'Tanh'
    initialization: str = 'default'

    def __post_init__(self):
        object.__setattr__(self, 'hidden_sizes', tuple(self.hidden_sizes))
        if not 0 <= len(self.hidden_sizes) <= MAX_HIDDEN_LAYERS or any(type(n) is not int or not 1 <= n <= MAX_NEURONS for n in self.hidden_sizes):
            raise ValueError('Use 0–8 hidden layers, each with 1–2048 neurons (bounded local model budget)')
        if self.activation not in ACTIVATIONS: raise ValueError('Unsupported hidden activation')
        if self.initialization not in INITIALIZATIONS: raise ValueError('Unsupported initialization')


def initialize(network, method):
    import torch
    if method == 'default': return
    for layer in network:
        if isinstance(layer, nn.Linear):
            if method == 'orthogonal': nn.init.orthogonal_(layer.weight)
            elif method == 'xavier_uniform': nn.init.xavier_uniform_(layer.weight)
            else: raise ValueError('Unsupported initialization')
            nn.init.zeros_(layer.bias)


def atomic_json(path, value):
    path = Path(path); path.parent.mkdir(parents=True,exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix='.'+path.name, suffix='.tmp')
    try:
        with os.fdopen(fd,'w') as stream: json.dump(value,stream,indent=2,allow_nan=False)
        os.replace(temporary,path)
    finally: Path(temporary).unlink(missing_ok=True)


def template(name, actor=None, critic=None):
    if not isinstance(name,str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,63}',name):
        raise ValueError('Name needs 1–64 letters, numbers, underscores or hyphens')
    def settings(design):
        data=asdict(design);data['hidden_sizes']=list(data['hidden_sizes']);return data
    return dict(schema_version=1,name=name,kind='model_template',status='untrained_configuration',
                observation_size=205,actions=32,actor=settings(actor or NetworkDesign()),critic=settings(critic or NetworkDesign()))


def validate_template(data):
    if data.get('schema_version')!=1 or data.get('kind')!='model_template' or data.get('observation_size')!=205 or data.get('actions')!=32:
        raise ValueError('Unsupported template interface or schema')
    return template(data['name'],NetworkDesign(**data['actor']),NetworkDesign(**data['critic']))


def save_template(data, directory=TEMPLATE_DIR):
    data=validate_template(data);path=Path(directory)/(data['name']+'.json')
    if path.exists(): raise ValueError('Template already exists; use a new name to duplicate it')
    atomic_json(path,data);return path


def load_template(path):
    return validate_template(json.loads(Path(path).read_text()))
