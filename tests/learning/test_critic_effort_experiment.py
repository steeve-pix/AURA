from copy import deepcopy
from dataclasses import asdict
import hashlib
from pathlib import Path
from types import SimpleNamespace
import unittest
from python.experiments.critic_effort_experiment import LABELS, assert_matches, validate_reference
from python.experiments.critic_effort_report import comparisons
from python.ppo_experiment import PPOConfig


class CriticEffortTests(unittest.TestCase):
    def setUp(self):
        self.env=SimpleNamespace(environment_contract={'quality_version':3})
        self.reference=dict(protocol_version=2,duration_seconds=1.,source={'sha256':'source'},
            environment_contract=self.env.environment_contract,simulator_sha256='simulator',
            updates=3,episodes_per_update=32,critic_lr=.001,critic_hidden_sizes=[64,64],
            gamma=.995,critic_return_scale=100.,heldout_episodes_per_update=8,
            evaluation_streams=[190042,190043,190044],ppo=asdict(PPOConfig()),branches=[])
        for label in LABELS:
            seed=42 if label=='warmstart_ppo' else int(label.rsplit('seed',1)[1])
            self.reference['branches'].append(dict(label=label,actor_seed=seed,warmstart=label=='warmstart_ppo',
                algorithm='ppo',actor_lr=.0001,actor_hidden_sizes=[64,64],measurements=[dict(update=i,
                training_seed=290000+seed*100+i,validation_seed=390000+seed*100+i) for i in range(4)]))
        paths=('python/arena_env.py','python/ppo_experiment.py','python/train_nn.py',
               'python/aura_policy.py','python/balance_quality.py','python/training_strategy_experiment.py')
        self.manifest={'source_files':{p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in paths}}

    def validate(self,reference=None,manifest=None):
        return validate_reference(reference or self.reference,self.env,'source','simulator',manifest or self.manifest)

    def test_valid_controls_are_retained_without_mutation(self):
        before=deepcopy(self.reference)
        self.assertEqual(set(self.validate()),set(LABELS))
        self.assertEqual(before,self.reference)

    def test_rejects_changed_objective_rates_seeds_or_fit_effort(self):
        for key,value in (('critic_lr',.0001),('episodes_per_update',64),('gamma',.99),
                          ('environment_contract',{}),('simulator_sha256','changed')):
            reference=deepcopy(self.reference);reference[key]=value
            with self.assertRaises(ValueError):self.validate(reference)
        reference=deepcopy(self.reference);reference['ppo']['critic_steps']=20
        with self.assertRaises(ValueError):self.validate(reference)
        reference=deepcopy(self.reference);reference['branches'][0]['measurements'][1]['training_seed']+=1
        with self.assertRaises(ValueError):self.validate(reference)
        reference=deepcopy(self.reference);reference['branches'][0]['warmstart']=False
        with self.assertRaises(ValueError):self.validate(reference)

    def test_rejects_changed_implementation(self):
        manifest=deepcopy(self.manifest);manifest['source_files']['python/ppo_experiment.py']='changed'
        with self.assertRaisesRegex(ValueError,'implementation'):self.validate(manifest=manifest)

    def test_replay_checks_nested_individual_results_and_undefined_values(self):
        data=dict(metrics=[{'reward':.1,'state':'FALLEN','ev':None}],valid=True)
        assert_matches(deepcopy(data),data)
        changed=deepcopy(data);changed['metrics'][0]['reward']=.2
        with self.assertRaisesRegex(ValueError,'reward'):assert_matches(changed,data)
        with self.assertRaises(ValueError):assert_matches([],data['metrics'])

    def example_pair(self):
        def measurement(update,seconds):
            return dict(update=update,evaluation=dict(stochastic_balanced_seconds=seconds,
                deterministic=dict(balanced_seconds=seconds),stochastic_fall_rate=1.),
                critic_heldout_after=dict(explained_variance=.5))
        return dict(label='seed42',actor_seed=42,warmstart=False,
            one_fit_control=dict(measurements=[measurement(0,.4),measurement(1,9.),measurement(2,.5)]),
            measurements=[measurement(0,.4),measurement(1,9.),measurement(2,.6)])

    def test_report_compares_prespecified_last_batch_not_best_observed(self):
        rows=comparisons(dict(branches=[self.example_pair()]))
        self.assertAlmostEqual(rows[0]['paired_difference'],.1)
        self.assertAlmostEqual(rows[0]['twenty_fit_change'],.2)

    def test_report_rejects_unmatched_initial_policies(self):
        branch=self.example_pair()
        branch['measurements'][0]['evaluation']['stochastic_balanced_seconds']=.3
        with self.assertRaisesRegex(ValueError,'initial balance'):comparisons(dict(branches=[branch]))


if __name__=='__main__':unittest.main()
