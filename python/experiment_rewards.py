"""Explicit experimental aggregations; active reward and termination stay intact."""
from dataclasses import asdict
from math import prod
from .balance_quality import quality_implementation

FORMULAS = ('product', 'geometric_mean', 'arithmetic_mean', 'minimum')


def reward_settings(version, formula='product', parameters=None):
    if formula not in FORMULAS: raise ValueError('Unsupported reward formula')
    config, _ = quality_implementation(version)
    return dict(formula=formula,parameters=asdict(config(**(parameters or {}))),schema_version=1)


def apply_reward(env, settings):
    if settings is None: return
    config, components = quality_implementation(env.quality_version)
    configured = config(**settings['parameters'])
    formula = settings['formula']
    if formula not in FORMULAS: raise ValueError('Unsupported reward formula')
    def calculate(obs):
        result = components(obs, configured)
        values = [value for key,value in result.items() if key!='balance_quality']
        result['balance_quality'] = (prod(values) if formula=='product' else
            prod(values)**(1/len(values)) if formula=='geometric_mean' else
            sum(values)/len(values) if formula=='arithmetic_mean' else min(values))
        return result
    env.quality_components = calculate
    env.experimental_reward = settings
