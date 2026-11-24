import unittest
from python.experiments.training_strategy_report import summaries


class StrategyReportTests(unittest.TestCase):
    def test_changes_use_each_architectures_own_baseline_and_final_update(self):
        branches=[]
        for width,values in ((64,(10.,99.,11.)),(128,(1.,99.,3.))):
            measurements=[]
            for i,value in enumerate(values):
                measurements.append(dict(update=i,evaluation=dict(stochastic_balanced_seconds=value,
                    deterministic=dict(balanced_seconds=value),stochastic_fall_rate=1.),
                    policy_change=dict(mean_kl=.01),optimization=dict(actor_optimizer_steps=2),
                    critic_heldout_after=dict(explained_variance=.5)))
            branches.append(dict(label=f'capacity_{width}',actor_seed=42,algorithm='ppo',
                                 actor_lr=.0001,actor_hidden_sizes=[width,width],measurements=measurements))
        rows=summaries(dict(branches=branches))
        self.assertEqual([r['stochastic_change'] for r in rows],[1.,2.])
        self.assertEqual([r['final_stochastic_seconds'] for r in rows],[11.,3.])
        self.assertEqual(rows[0]['accepted_actor_steps'],4)


if __name__=='__main__':unittest.main()
