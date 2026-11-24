"""Current production checkpoints must remain selectable after being saved."""
import hashlib
import tempfile
import unittest
from pathlib import Path
import torch
from cli.backend.model_viewer import model_catalog
from cli.backend.training_run import TrainingRun
from python.train_nn import TrainingConfig,create_training,create_critic
from python.model_checkpoint import save_checkpoint
from python.experiment_rewards import reward_settings

class ModelCatalogTests(unittest.TestCase):
    def test_standalone_independent_critics_match_their_collector_and_reject_old_rewards(self):
        import aura_python
        from python.independent_env import IndependentEnv
        from python.aura_env import OBSERVATION_SCHEMA_VERSION
        from python.aura_policy import ACTION_SCHEMA_VERSION,AuraValue
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'critics'/'fixture'/'critic.pt'
            path.parent.mkdir(parents=True)
            env=IndependentEnv(1)
            current=dict(kind='aura_value',**env.environment_contract,
                         observation_size=env.observation_size,observation_schema_version=OBSERVATION_SCHEMA_VERSION,
                         action_schema_version=ACTION_SCHEMA_VERSION,physical_action_schema_version=aura_python.action_schema_version,
                         model=AuraValue(env.observation_size).state_dict())
            old=IndependentEnv(1,quality_version=2)
            for data,expected in ((current,'compatible'),
                                  ({**current,**old.environment_contract},'incompatible'),
                                  ({**current,'physical_action_schema_version':-1},'incompatible')):
                torch.save(data,path)
                before=path.read_bytes()
                rows=[line.split('\t') for line in model_catalog(folder) if line.startswith('AURA_MODEL\t')]
                self.assertEqual(rows[0][2],expected)
                self.assertEqual(path.read_bytes(),before)

    def test_production_reward_metadata_allows_resume_and_experiments_do_not(self):
        config=TrainingConfig(batch_size=2,agents=1)
        env,actor,optimizer=create_training(config)
        critic,critic_optimizer=create_critic(env,config)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'fixture.pt'
            save_checkpoint(actor,optimizer,0,path,critic=critic,critic_optimizer=critic_optimizer)
            original=torch.load(path,weights_only=True)
            self.assertIsNone(original['experimental_reward'])
            for reward,expected in ((None,'compatible'),(reward_settings(env.quality_version,'geometric_mean',{}),'historical')):
                data=dict(original,experimental_reward=reward)
                data['critic']=dict(original['critic'],environment_contract=dict(original['critic']['environment_contract'],experimental_reward=reward))
                torch.save(data,path)
                digest=hashlib.sha256(path.read_bytes()).digest()
                models=[line.split('\t') for line in model_catalog(folder) if line.startswith('AURA_MODEL\t')]
                self.assertEqual(models[0][2],expected)
                self.assertEqual(hashlib.sha256(path.read_bytes()).digest(),digest)
            torch.save(original,path)
            digest=hashlib.sha256(path.read_bytes()).digest()
            with TrainingRun(config,'resume',checkpoint=path,directory=folder) as run:
                self.assertEqual(run.episodes_completed,0)
                self.assertEqual(run.policy.design,actor.design)
            self.assertEqual(hashlib.sha256(path.read_bytes()).digest(),digest)
