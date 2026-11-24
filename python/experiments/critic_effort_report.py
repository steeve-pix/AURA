"""Paired physical outcomes and value fitting; no automatic model promotion."""
import argparse
import csv
import json
from pathlib import Path
from statistics import mean


def comparisons(result):
    rows=[]
    for branch in result['branches']:
        control=branch['one_fit_control']
        a,b=control['measurements'][-1],branch['measurements'][-1]
        initial=branch['measurements'][0]['evaluation']['stochastic_balanced_seconds']
        control_initial=control['measurements'][0]['evaluation']['stochastic_balanced_seconds']
        if abs(initial-control_initial)>1e-9:raise ValueError('Paired initial balance differs')
        rows.append(dict(label=branch['label'],seed=branch['actor_seed'],warmstart=branch['warmstart'],
            initial_stochastic_seconds=initial,
            one_fit_seconds=a['evaluation']['stochastic_balanced_seconds'],
            twenty_fit_seconds=b['evaluation']['stochastic_balanced_seconds'],
            paired_difference=b['evaluation']['stochastic_balanced_seconds']-a['evaluation']['stochastic_balanced_seconds'],
            one_fit_change=a['evaluation']['stochastic_balanced_seconds']-initial,
            twenty_fit_change=b['evaluation']['stochastic_balanced_seconds']-initial,
            one_fit_deterministic_seconds=a['evaluation']['deterministic']['balanced_seconds'],
            twenty_fit_deterministic_seconds=b['evaluation']['deterministic']['balanced_seconds'],
            one_fit_final_ev=a['critic_heldout_after']['explained_variance'],
            twenty_fit_final_ev=b['critic_heldout_after']['explained_variance'],
            twenty_fit_fall_rate=b['evaluation']['stochastic_fall_rate']))
    return rows


def report(folder):
    folder=Path(folder);result=json.loads((folder/'results.json').read_text())
    if 'duration_seconds' not in result:raise ValueError('Experiment still running')
    rows=comparisons(result)
    with (folder/'summary.csv').open('w',newline='') as handle:
        writer=csv.DictWriter(handle,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    lines=['# AURA — increased critic fitting comparison','',
        '16 October 2026. This experiment changes critic fitting effort only; it does not promote a new production trainer.','',
        '## Matched protocol','',
        '- One versus twenty full-batch critic Adam steps after each actor update. Extra fits reuse the same 32 collected training episodes; they do not add physical training episodes.',
        '- Three trajectory batches per branch, four arena agents, 12 m spacing, collisions enabled, CPU with one tensor thread. Both networks remain 64×64 with their existing Tanh activations; 205 inputs and 32 actions are unchanged.',
        '- PPO-Clip with normalized Monte Carlo advantages, actor LR .0001, critic LR .001, eight maximum actor optimizer steps and exact empirical mean-KL limit .01. Gamma .995 and return scale 100 remain unchanged.',
        '- Critic fitting happens after the actor update with detached pre-fit advantages. More critic fits first affect the following batch, not the current batch.',
        '- Independent validation: eight held-out episodes per batch, never used for fitting. Each evaluation uses four deterministic agents and twelve stochastic episodes from three matched RNG streams.',
        '- One saved best actor plus fresh actor seeds 42, 43 and 44. Fresh critics and optimizers in every branch. Warm-start and fresh seed 42 are reported separately.',
        f"- Saved actor SHA-256: {result['source']['sha256']}.",
        f"- Previous one-fit control snapshot SHA-256: {result['reference']['sha256']}.",
        '- Control reuse validates simulator hash, full environment contract, relevant implementation hashes, optimizer settings and sampling seeds. Every first batch replays the previous one-fit optimization, held-out metrics and evaluation. The one-fit and twenty-fit actors are also checked to have identical first-update weights.',
        '', '## Prespecified final outcomes — stochastic balanced seconds','',
        '| Actor | Initial | One fit | Twenty fits | Twenty minus one |',
        '| --- | ---: | ---: | ---: | ---: |']
    for row in rows:
        name='Saved best actor' if row['warmstart'] else f"Fresh seed {row['seed']}"
        lines.append(f"| {name} | {row['initial_stochastic_seconds']:.4f} | {row['one_fit_seconds']:.4f} | {row['twenty_fit_seconds']:.4f} | {row['paired_difference']:+.4f} |")
    fresh=[row for row in rows if not row['warmstart']]
    lines+=['',
        f"Fresh-seed means: initial {mean(row['initial_stochastic_seconds'] for row in fresh):.4f} s; one fit {mean(row['one_fit_seconds'] for row in fresh):.4f} s; twenty fits {mean(row['twenty_fit_seconds'] for row in fresh):.4f} s. "
        f"Twenty fits outperform one fit on {sum(row['paired_difference']>1e-9 for row in fresh)}/{len(fresh)} fresh seeds "
        f"and improve over their own initialization on {sum(row['twenty_fit_change']>1e-9 for row in fresh)}/{len(fresh)} seeds.",
        '', '## Critic predictions and policy change by batch','',
        'Held-out targets are frozen before each actor update, under the collection policy. In batch one both settings use identical validation trajectories, so their prediction errors are directly comparable. Later actor policies and hence validation trajectories can differ. Higher explained variance on those different distributions alone does not establish better control.',
        '', '| Actor | Batch | One-fit held-out EV | Twenty-fit held-out EV | Twenty-fit raw RMSE | Twenty-fit mean KL |',
        '| --- | ---: | ---: | ---: | ---: | ---: |']
    def number(value):return 'undefined' if value is None else f'{value:.4f}'
    for branch in result['branches']:
        name='Saved best' if branch['warmstart'] else f"Seed {branch['actor_seed']}"
        for a,b in zip(branch['one_fit_control']['measurements'][1:],branch['measurements'][1:]):
            va,vb=a['critic_heldout_after'],b['critic_heldout_after']
            lines.append(f"| {name} | {b['update']} | {number(va['explained_variance'])} | {number(vb['explained_variance'])} | {vb['rmse_raw']:.3f} | {b['policy_change']['mean_kl']:.5f} |")
    lines+=['','## Interpretation limits and preserved state','',
        '- Judge better fitting by independent validation and better control by physical balanced duration. Training MSE or return prediction alone is not success.',
        '- Three batches and three fresh seeds remain a small pilot. The arena agents are coupled samples, not independent training seeds. No unseen starting postures, disturbances or recovery curriculum are tested.',
        '- Repeated fitting can overfit or make the value predictor stale when the policy changes; 20 is a tested candidate, not a universal ideal.',
        '- PPO settings are fixed, but accepted optimizer step counts and episode lengths can differ as policies diverge. Full JSON preserves individual results, training returns/lengths, KL, clip fractions and critic diagnostics.',
        '- Original checkpoints, previous experiment files, production training and CLI defaults remain unchanged. Experimental paired actor/critic files are saved only in this report directory and are marked non-production-resumable.',
        '', 'Reproduce from the repository root with an unused output directory:', '', '```sh',
        'PYTHONPATH=build-release:. python3 -m python.experiments.critic_effort_experiment --output reports/critic-effort-new',
        'PYTHONPATH=build-release:. python3 -m python.experiments.critic_effort_report reports/critic-effort-new',
        '```','']
    (folder/'report.md').write_text('\n'.join(lines))
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folder',type=Path)
    args=parser.parse_args();report(args.folder)
