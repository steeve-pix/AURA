"""Immutable experiments, explicit compatibility and physical smoke outcomes."""
import json
import hashlib
from pathlib import Path
import tempfile
import unittest
import torch
from python.experiment_library import configuration,validate,create_experiment,run_experiment,promote
from python.model_templates import template,save_template,load_template,NetworkDesign
from python.independent_env import IndependentEnv
from python.train_nn import TrainingConfig,create_training,create_critic
from python.model_checkpoint import save_checkpoint,load_checkpoint


class ExperimentLibraryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(1)

    def settings(self,name='smoke'):
        c=configuration(name);c.update(episodes=2,updates=1,workers=2,actor_hidden=[8],critic_hidden=[8],training_seeds=[42],evaluation_seeds=[10042])
        return c

    def test_template_is_configuration_not_checkpoint_and_duplicate_is_safe(self):
        with tempfile.TemporaryDirectory() as folder:
            data=template('relu_design',NetworkDesign((8,12),'ReLU'))
            path=save_template(data,folder)
            self.assertEqual(load_template(path)['status'],'untrained_configuration')
            self.assertNotIn('model',json.loads(path.read_text()))
            original=path.read_bytes()
            with self.assertRaises(ValueError):save_template(data,folder)
            self.assertEqual(path.read_bytes(),original)

    def test_validation_rejects_unknown_formulas_architecture_and_overlapping_seeds(self):
        for changed in ({'reward_formula':'eval(expression)'},{'actor_activation':'unknown'},
                        {'actor_hidden':[0]},{'evaluation_seeds':[42]},{'starting_condition':'unimplemented_tilt'}):
            with self.subTest(changed=changed),self.assertRaises(ValueError):validate({**self.settings(),**changed})
        self.assertEqual(validate({**self.settings(),'workers':5})['workers'],5)
        c=self.settings();c.update(comparison='actor_hidden',values=[[8],[16,16]])
        self.assertEqual(len(validate(c)['values']),2)

    def test_matched_worker_benchmark_records_seeds_processes_and_immutable_results(self):
        with tempfile.TemporaryDirectory() as directory:
            c=self.settings();c.update(kind='benchmark',comparison='workers',values=[1])
            path=create_experiment(c,directory)
            stages=[]
            rows=run_experiment(path,diagnostic_limit=4,stage=stages.append)
            self.assertTrue(any('measuring collection throughput' in stage for stage in stages))
            self.assertEqual(rows[0]['episode_seeds'],rows[1]['episode_seeds'])
            self.assertEqual(json.loads((path/'state.json').read_text())['status'],'completed')
            self.assertGreater(rows[0]['episodes_per_second'],0)
            with self.assertRaises(ValueError):run_experiment(path,diagnostic_limit=4)
            with self.assertRaisesRegex(ValueError,'Diagnostic'):promote(path,directory=Path(directory)/'templates')

    def test_cancel_is_persisted_and_source_checkpoint_never_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            cfg=TrainingConfig(agents=1,actor_design=NetworkDesign((8,)),critic_design=NetworkDesign((8,)))
            env,actor,opt=create_training(cfg);critic,co=create_critic(env,cfg)
            source=save_checkpoint(actor,opt,0,Path(directory)/'source.pt',critic=critic,critic_optimizer=co)
            before=source.read_bytes();c=self.settings();c.update(source=str(source),initialization_mode='transfer')
            path=create_experiment(c,directory)
            def cancel():raise KeyboardInterrupt
            with self.assertRaises(KeyboardInterrupt):run_experiment(path,service=cancel,diagnostic_limit=4)
            self.assertEqual(json.loads((path/'state.json').read_text())['status'],'cancelled')
            self.assertEqual(source.read_bytes(),before)

    def test_reward_audit_compares_three_frozen_actors_with_matched_rollouts(self):
        with tempfile.TemporaryDirectory() as directory:
            cfg=TrainingConfig(agents=1,actor_design=NetworkDesign((8,)),critic_design=NetworkDesign((8,)))
            env,actor,opt=create_training(cfg)
            sources=[]
            critic,critic_optimizer=create_critic(env,cfg)
            for seed in (42,43,44):
                source=save_checkpoint(actor,opt,0,Path(directory)/f'control-seed{seed}.pt',critic=critic,critic_optimizer=critic_optimizer)
                sources.append(str(source))
            c=self.settings();c.update(kind='reward_audit',source=sources[0],initialization_mode='transfer',comparison='source',values=sources[1:])
            path=create_experiment(c,Path(directory)/'experiments')
            hashes=[hashlib.sha256(Path(source).read_bytes()).hexdigest() for source in sources]
            # All actors must run from reviewed snapshots, even if originals disappear.
            for source in sources:Path(source).unlink()
            rows=run_experiment(path,diagnostic_limit=4)
            self.assertEqual(len(rows),3)
            self.assertEqual([r['source_checkpoint'] for r in rows],sources)
            self.assertEqual([r['source_sha256'] for r in rows],hashes)
            self.assertEqual(rows[0]['episode_seeds'],rows[1]['episode_seeds'])
            self.assertEqual(rows[1]['episode_seeds'],rows[2]['episode_seeds'])
            self.assertTrue(all(not row['updates'] for row in rows))
            self.assertTrue(all('body_rotation' in row['mean_components'] for row in rows))
            # Identical actor weights and matched seeds must yield identical audits.
            self.assertEqual(rows[0]['mean_components'],rows[1]['mean_components'])
            self.assertEqual(rows[1]['mean_components'],rows[2]['mean_components'])

    def test_source_comparison_rejects_duplicates_and_non_audit_studies(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'actor.pt';torch.save({},source)
            c=self.settings();c.update(kind='reward_audit',source=str(source),initialization_mode='transfer',comparison='source',values=[str(source)])
            with self.assertRaisesRegex(ValueError,'distinct'):validate(c)
            c.update(kind='training')
            with self.assertRaisesRegex(ValueError,'Unsupported comparison'):validate(c)

    def test_one_training_update_saves_dynamic_network_pair_and_finite_metrics(self):
        with tempfile.TemporaryDirectory() as directory:
            c=self.settings();c.update(kind='training',actor_activation='ReLU',critic_activation='Sigmoid')
            path=create_experiment(c,directory);rows=run_experiment(path,diagnostic_limit=8)
            saved=Path(rows[0]['checkpoint']);data=torch.load(saved,weights_only=True)
            env=IndependentEnv(3)
            from python.experiment_rewards import apply_reward
            apply_reward(env,data['experimental_reward'])
            actor,_=load_checkpoint(saved,env)
            self.assertIsInstance(actor.network[1],torch.nn.ReLU)
            self.assertEqual(data['critic']['design']['activation'],'Sigmoid')
            self.assertIn('episode_seed',rows[0]['updates'][0]['episodes'][0])
            self.assertTrue(all(torch.isfinite(p).all() for p in actor.parameters()))
            self.assertIn('critic_accuracy',rows[0]['final'])

    def test_failed_source_records_error_without_overwriting_source(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'invalid.pt';source.write_bytes(b'not a checkpoint')
            c=self.settings();c['source']=str(source);c['initialization_mode']='transfer'
            with self.assertRaises(Exception):create_experiment(c,directory)
            self.assertEqual(source.read_bytes(),b'not a checkpoint')

    def test_critic_fit_keeps_actor_frozen_and_reports_held_out_accuracy(self):
        with tempfile.TemporaryDirectory() as directory:
            c=self.settings();c.update(kind='critic_fit',critic_fits=2)
            path=create_experiment(c,directory);rows=run_experiment(path,diagnostic_limit=8)
            row=rows[0];saved=torch.load(row['checkpoint'],weights_only=True)
            self.assertEqual(row['initial']['mean'],row['final']['mean'])
            self.assertEqual(row['updates'][0]['gradient_norm'],0.)
            self.assertEqual(saved['updates_completed'],0)
            self.assertTrue(any(int(v['step'])==2 for v in saved['critic']['optimizer']['state'].values()))
            self.assertIn('critic_accuracy',row['final'])

    def test_ppo_experiment_is_explicit_and_source_protected(self):
        with tempfile.TemporaryDirectory() as directory:
            c=self.settings();c.update(kind='training',algorithm='ppo_clip')
            path=create_experiment(c,directory);rows=run_experiment(path,diagnostic_limit=8)
            saved=torch.load(rows[0]['checkpoint'],weights_only=True)
            self.assertEqual(saved['training_algorithm'],'ppo_clip')
            self.assertTrue(all(torch.isfinite(v).all() for v in saved['model'].values()))

    def test_comparison_artifact_and_snapshot_tamper_protection(self):
        with tempfile.TemporaryDirectory() as directory:
            c=self.settings();c.update(comparison='actor_hidden',values=[[12]])
            path=create_experiment(c,directory);run_experiment(path,diagnostic_limit=4)
            paired=json.loads((path/'comparison.json').read_text())['paired']
            self.assertEqual(paired[0]['training_seed'],42)
            self.assertIn('balanced_seconds',paired[0]['final_difference'])
            cfg=TrainingConfig(agents=1,actor_design=NetworkDesign((8,)),critic_design=NetworkDesign((8,)))
            env,actor,opt=create_training(cfg);critic,co=create_critic(env,cfg)
            source=save_checkpoint(actor,opt,0,Path(directory)/'source.pt',critic=critic,critic_optimizer=co)
            c=self.settings('snapshot');c.update(source=str(source),initialization_mode='transfer')
            path=create_experiment(c,directory);(path/'source_snapshot.pt').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'hash'):run_experiment(path,diagnostic_limit=4)

    def test_evaluation_seeds_cannot_overlap_derived_training_episode_seeds(self):
        with self.assertRaisesRegex(ValueError,'assigned training'):
            validate({**self.settings(),'kind':'training','evaluation_seeds':[1000045]})

    def test_irrelevant_comparisons_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'comparison'):
            validate({**self.settings(),'kind':'baseline','comparison':'actor_lr','values':[.0001]})


if __name__=='__main__':unittest.main()
