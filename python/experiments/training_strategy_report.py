"""Summarize prespecified final outcomes; retain every update and evaluation."""
import argparse
import csv
import json
from pathlib import Path
from statistics import mean


def summaries(result):
    rows=[]
    for branch in result['branches']:
        first,last=branch['measurements'][0],branch['measurements'][-1]
        start,end=first['evaluation'],last['evaluation']
        rows.append(dict(label=branch['label'],seed=branch['actor_seed'],algorithm=branch['algorithm'],
            width=branch['actor_hidden_sizes'][0],actor_lr=branch['actor_lr'],updates=last['update'],
            initial_stochastic_seconds=start['stochastic_balanced_seconds'],
            final_stochastic_seconds=end['stochastic_balanced_seconds'],
            stochastic_change=end['stochastic_balanced_seconds']-start['stochastic_balanced_seconds'],
            initial_deterministic_seconds=start['deterministic']['balanced_seconds'],
            final_deterministic_seconds=end['deterministic']['balanced_seconds'],
            stochastic_fall_rate=end['stochastic_fall_rate'],
            mean_update_kl=mean(m['policy_change']['mean_kl'] for m in branch['measurements'][1:]),
            accepted_actor_steps=sum(m['optimization']['actor_optimizer_steps'] for m in branch['measurements'][1:]),
            final_heldout_ev=last['critic_heldout_after']['explained_variance']))
    return rows


def report(folder):
    folder=Path(folder);r=json.loads((folder/'results.json').read_text())
    if 'duration_seconds' not in r:
        raise ValueError('Experiment still running; final comparison is not ready')
    rows=summaries(r)
    with (folder/'summary.csv').open('w',newline='') as handle:
        writer=csv.DictWriter(handle,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    lines=['# AURA training-strategy pilot — 16 October 2026','',
        'This is a bounded diagnostic comparison, not evidence of convergence or robust balance.','',
        '## Protocol','',
        f"- Reward/physics contract: `{r['environment_contract']}`",
        f"- Frozen warm-start actor: {r['source']['filename']} / SHA-256 {r['source']['sha256']}.",
        f"- {r['updates']} updates per branch; {r['episodes_per_update']} training episodes/update; four agents; saved arena spacing and collisions unchanged.",
        '- CPU with one tensor thread; fixed 120 Hz physics; 205 observations and 32 actions.',
        '- Every evaluation: four deterministic agents plus three matched stochastic streams × four agents.',
        '- Fresh critic and Adam states in every training branch; critic architecture 64×64 Tanh and LR .001.',
        '- Eight held-out episodes per update collected before fitting, independent of the training/evaluation RNG streams. The same frozen held-out targets are scored before/after fitting; these are returns under the collection policy.',
        '- Historical critics are evaluated against their own saved reward version, never substituted into new-reward training.',
        '', '## Update diagnostics and controlled optimization','',
        'The original .001 MC update is retained as the reference. The .0001 MC control isolates a smaller actor learning rate. PPO uses .0001, normalized frozen Monte Carlo advantages, eight maximum full-batch actor steps, clip ratio .2, and an empirical exact mean Gaussian KL limit .01. Critic fitting remains one step per batch in all branches.',
        '',
        'PPO retains raw latent samples and their original log probabilities. Executed motor commands alone cannot reconstruct those probabilities after clipping. A proposed KL overshoot restores both actor parameters and Adam state and ends the batch update. The KL bound applies on the collected states, not all possible states. This is PPO-Clip with an additional rollback guard, not a claim of canonical/default PPO settings.',
        '',
        'PPO versus the smaller-step MC arm tests a package of normalized advantages, repeated clipped optimization and KL guarding; it does not isolate clipping alone. Rate selection was based on update-size measurements, not tuning against evaluation outcomes.',
        '', '## Historical critic accuracy','',
        '| Checkpoint | Reward quality version | Held-out raw-return RMSE | Explained variance |',
        '| --- | ---: | ---: | ---: |']
    for d in r['checkpoint_diagnostics']:
        v=d['heldout'];lines.append(f"| {d['label']} | {d['quality_version']} | {v['rmse_raw']:.3f} | {v['explained_variance']:.4f} |")
    lines+=['','High return-prediction accuracy for a failing policy does not demonstrate useful motor control or establish critic accuracy on recovery states. Explained variance measures residual variance, so the report also retains RMSE and zero-predictor MSE.','',
        '## Final outcomes — balanced seconds','',
        '| Branch | Initial stochastic | Final stochastic | Change | Final deterministic | Mean update KL | Actor steps accepted |',
        '| --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for v in rows:
        lines.append(f"| {v['label']} | {v['initial_stochastic_seconds']:.4f} | {v['final_stochastic_seconds']:.4f} | {v['stochastic_change']:+.4f} | {v['final_deterministic_seconds']:.4f} | {v['mean_update_kl']:.5f} | {v['accepted_actor_steps']} |")
    warm={row['label']:row for row in rows if row['label'].startswith('warmstart_')}
    if all(key in warm for key in ('warmstart_mc','warmstart_mc_small_step','warmstart_ppo')):
        control=warm['warmstart_mc_small_step'];ppo=warm['warmstart_ppo']
        lines+=['',
            f"PPO finishes {ppo['final_stochastic_seconds']-control['final_stochastic_seconds']:+.4f} s relative to the smaller-step MC control on this warm-start actor. "
            'Compare that result with the individual trajectories above; controlling KL does not itself guarantee a better physical policy.']
    capacity=[v for v in rows if v['label'].startswith('capacity_')]
    if capacity:
        lines+=['','## Capacity comparison','',
            'Only actor width changes. The critic, reward, algorithm, rates, episode budget and evaluation streams are fixed. Widths cannot share identical initial tensors; compare changes from each seed/width’s own baseline. Seeds are the experimental replicates; the four coupled arena agents are not four independent training runs.','',
            '| Actor width | Mean initial stochastic seconds | Mean final stochastic seconds | Mean change | Improved seeds |',
            '| ---: | ---: | ---: | ---: | ---: |']
        for width in (64,128):
            values=[v for v in capacity if v['width']==width]
            lines.append(f"| {width} | {mean(v['initial_stochastic_seconds'] for v in values):.4f} | {mean(v['final_stochastic_seconds'] for v in values):.4f} | {mean(v['stochastic_change'] for v in values):+.4f} | {sum(v['stochastic_change']>0 for v in values)}/{len(values)} |")
        lines+=['',
            'Different widths start at substantially different balance levels. Zero-duration baselines also have a floor effect: they cannot regress below zero. Neither raw final means nor changes alone establish a causal capacity advantage with three seeds.']
    if r.get('calibration'):
        lines+=['','## Retained calibration attempt','',
            'The initial PPO attempt used actor LR .001. It is retained here rather than hidden:']
        for trial in r['calibration']:
            for m in trial['measurements'][1:]:
                o=m['optimization'];lines.append(f"- Update {m['update']}: proposed mean KL {o['max_attempted_kl']:.5f}; accepted actor steps {o['actor_optimizer_steps']}; post-guard KL {o['mean_kl']:.5f}.")
    curve=folder/'critic-fit-curve.json'
    if curve.exists():
        fit=json.loads(curve.read_text())
        lines+=['','## Fixed-data critic fitting curve','',
            'Separate diagnostic using 32 fixed training episodes and eight independent held-out episodes. The actor is frozen and never optimized. Repeating critic fits changes compute, not the environment episode budget. This is not a tuned training winner or a guarantee of accuracy on new recovery states.','',
            '| Critic fit steps | Held-out explained variance | Held-out raw-return RMSE |',
            '| ---: | ---: | ---: |']
        for measurement in fit['measurements']:
            v=measurement['heldout']
            lines.append(f"| {measurement['optimizer_steps']} | {v['explained_variance']:.4f} | {v['rmse_raw']:.3f} |")
    lines+=['','## Scope and reproducibility','',
        '- Full JSON retains all per-update diagnostics, per-agent deterministic/stochastic evaluations and every training episode return/length. Summary CSV does not replace individual results.',
        '- Experimental `.pt` files are explicitly tagged non-production-resumable and saved only in this report directory; the production trainer, CLI choices/defaults and existing models are unchanged.',
        '- Three updates and three fresh actor seeds are insufficient for declaring an architecture or optimizer generally superior. No recovery curriculum, disturbances or unseen starting postures are tested.',
        '- MC and PPO collect the same number of episodes, but optimizer step counts and policy-dependent episode lengths can differ. This is an environment-episode budget comparison, not equal optimizer compute.',
        '- PPO with an inaccurate critic, fixed exploration or a weak reward can still fail. There is no automatic promotion of the best-looking pilot branch.',
        '',
        'Method reference: [OpenAI Spinning Up PPO](https://spinningup.openai.com/en/latest/algorithms/ppo.html).',
        '', 'Reproduce in an unused directory:', '', '```sh',
        'PYTHONPATH=build-release:. python3 -m python.training_strategy_experiment --output reports/strategy-new',
        'PYTHONPATH=build-release:. python3 -m python.experiments.training_strategy_report reports/strategy-new',
        '```','']
    (folder/'report.md').write_text('\n'.join(lines))
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folder',type=Path)
    args=parser.parse_args();report(args.folder)
