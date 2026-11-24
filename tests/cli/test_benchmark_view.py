"""Benchmark aggregation and bounded terminal rendering without starting workers."""
import unittest
from unittest.mock import patch
from cli.backend.benchmark_view import benchmark_groups,draw_benchmarks
from tests.cli import test_experiment_ui


class BenchmarkViewTests(unittest.TestCase):
    def rows(self):
        return [dict(branch=branch,seed=seed,updates=[{}],
            initial=dict(mean=dict(balanced_seconds=.2,fall_rate=1.)),
            final=dict(mean=dict(balanced_seconds=value,fall_rate=.5),
                       deterministic=dict(balanced_seconds=value*2,fall_rate=0.)),
            physics_steps_per_second=1000*value,collection_seconds=10/value)
            for branch,value in [('control',.4),('variant_1',.8)] for seed in [42,43,44]]

    def test_branch_means_and_real_units(self):
        groups=benchmark_groups(self.rows())
        balance=next(g for g in groups if g['key']=='balanced_seconds')
        self.assertEqual([s['count'] for s in balance['series']],[3,3])
        self.assertAlmostEqual(balance['series'][1]['value'],.8)
        self.assertFalse(next(g for g in groups if g['key']=='fall_rate')['higher'])
        self.assertTrue(next(g for g in groups if g['key']=='physics_steps_per_second')['higher'])
        deterministic=benchmark_groups(self.rows(),True)
        self.assertAlmostEqual(deterministic[0]['series'][1]['value'],1.6)

    def test_single_trained_branch_compares_before_after(self):
        groups=benchmark_groups(self.rows()[:3])
        self.assertEqual([s['label'] for s in groups[0]['series']],['control / before','control / after'])
        throughput=next(g for g in groups if g['key']=='physics_steps_per_second')
        self.assertEqual(len(throughput['series']),1)

    def test_missing_and_nonfinite_values_are_not_zero_measurements(self):
        rows=[dict(branch='control',final=dict(mean=dict(balanced_seconds=float('nan'))))]
        self.assertEqual(benchmark_groups(rows),[])

    def test_reward_components_are_first_and_named_for_each_actor(self):
        rows=self.rows()
        for row in rows:
            row['mean_components']=dict(posture=.5,body_rotation=.2)
            row['source_checkpoint']='/fixtures/control-seed'+('42' if row['branch']=='control' else '43')+'.pt'
        groups=benchmark_groups(rows)
        self.assertEqual(groups[0]['key'],'component:posture')
        self.assertEqual(groups[1]['key'],'component:body_rotation')
        self.assertIn('seed42',groups[0]['series'][0]['label'])
        self.assertIn('seed43',groups[0]['series'][1]['label'])
        self.assertAlmostEqual(groups[1]['series'][0]['value'],.2)

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_all_seven_audit_components_fit_wide_workspace_without_scrolling(self,_):
        rows=self.rows()
        for row in rows:row['mean_components']={key:.5 for key in ('posture','height','com_motion','body_rotation','support','stationary_hold','foot_grip')}
        ui=test_experiment_ui.ExperimentFormTests().ui([],180,50)
        ui.workspace_context=('fixture',3)
        draw_benchmarks(ui,rows,'fixture',components_only=True)
        shown=' '.join(ui.screen.lines.values())
        self.assertIn('1–7 of 7',shown)
        self.assertIn('Foot grip',shown)
        self.assertNotIn('Episode return',shown)

    @patch('cli.backend.experiment_ui.curses.color_pair',side_effect=lambda i:i)
    def test_chart_fits_terminals_and_scrolls_to_last_metric(self,_):
        for width,height in ((64,24),(100,34),(200,60)):
            ui=test_experiment_ui.ExperimentFormTests().ui([],width,height)
            draw_benchmarks(ui,self.rows(),'fixture')
            shown=' '.join(ui.screen.lines.values())
            self.assertIn('BENCHMARKS',shown)
            self.assertIn('█',shown)
            self.assertIn('control',shown)
            self.assertEqual(draw_benchmarks(ui,self.rows(),'fixture',999),3)
            self.assertIn('Collection time',' '.join(ui.screen.lines.values()))


if __name__=='__main__':unittest.main()
