# Critic fitting effort — 16 October 2026

Completed a bounded comparison of one versus twenty critic fits per collected
batch, keeping the actor and critic at 64×64. The [full report](../../reports/critic-effort-20261016/report.md)
contains all batches, individual agent evaluations, validation metrics and the
protocol. JSON and CSV exports sit beside it.

## Physical outcomes

Final stochastic balanced duration, in seconds:

| Actor | Initial | One critic fit | Twenty critic fits |
| --- | ---: | ---: | ---: |
| Saved best actor | .6153 | .4632 | .5625 |
| Fresh seed 42 | .3590 | .3903 | .4403 |
| Fresh seed 43 | .4035 | .4000 | .3521 |
| Fresh seed 44 | .0000 | .0382 | .0910 |

Across the three fresh seeds, final mean balance rises from .2762 s with one
fit to .2944 s with twenty: a difference of .0183 s. Twenty fits outperform
one fit on two of three fresh seeds. The saved actor regresses less but remains
below its initial .6153 s. Every evaluated episode still ends in a fall.

## What this establishes

The additional fits improve held-out value prediction substantially. Final
explained variance with twenty fits is .776 for the saved actor and
.797/.826/.544 for fresh seeds 42/43/44. However, seed 43 loses physical balance
despite stronger prediction. More accurate expected returns do not guarantee
better motor control or recovery.

First-batch validation trajectories are identical between each pair. The first
actor update is also identical: critic fitting happens afterward, so the extra
fits can only influence the next batch's baseline. All four first-batch replays
match their previous one-fit controls, including optimization, held-out metrics
and physical evaluations. The actor weights from the one-fit and twenty-fit
first updates match exactly.

Later validation trajectories differ as policies diverge. Their explained
variances therefore describe prediction under each collection policy, rather
than a common fixed distribution. The independent physical evaluation remains
the measure of control quality.

## Fixed configuration and safety of the experiment

- Experimental PPO-Clip, normalized Monte Carlo advantages, actor LR .0001,
  critic LR .001, at most eight actor optimizer steps and mean latent-Gaussian
  KL limit .01 on collected states. This is the earlier experimental pipeline,
  not the production MC trainer.
- Thirty-two physical training episodes per batch and three batches per actor;
  twenty critic optimizer steps reuse that same data. They do not mean twenty
  new episode batches or twenty new model files.
- Eight held-out validation episodes per batch; four deterministic and twelve
  stochastic evaluation episodes per measurement. Fresh seeds 42/43/44 plus a
  separate warm start from the saved best actor. Both networks, exploration,
  reward, physics, observations, actions and arena conditions stay fixed.
- One-fit controls are reused from the preceding experiment only after checking
  source/simulator/implementation hashes, full environment contract, settings
  and exploration seeds. The prior results are copied into the new output
  directory; they are not rewritten.
- Original saved models and previous results are unchanged. Candidate actor and
  critic states are saved together under `reports/`, explicitly marked as not
  resumable by production training. Production and CLI defaults are unchanged.

90 focused Python tests pass, including exact critic optimizer-step counts,
actor/critic gradient separation, first-update actor equality across fitting
efforts, reference compatibility checks and paired result calculations. All
accepted candidate policy changes respect the .01 empirical mean-KL limit.

## Decision and limitations

Keep 64×64 as the default architecture. Twenty critic fits are a promising
experimental setting for prediction and modest mean balance gains; this pilot
does not establish reliable actor improvement or justify automatic production
promotion. It also does not establish that twenty is optimal.

Only three trajectory batches and three fresh seeds were tested. The four
arena agents are coupled samples, not four separate actor seeds. Evaluation
streams were reused to obtain matched comparisons, not kept as an untouched
final benchmark. No new postures, disturbances or demonstrated recovery skill
are included. Further confirmation should use a longer bounded paired test
and fresh evaluation streams before adopting the setting.

Implementation: `python/experiments/critic_effort_experiment.py`,
`python/experiments/critic_effort_report.py`, `tests/learning/test_critic_effort_experiment.py`, and an
additional fitting-isolation check in `tests/learning/test_ppo_experiment.py`.
