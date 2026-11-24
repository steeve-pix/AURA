# Training strategy experiments — 16 October 2026

Completed the requested sequence: measure current update size and critic
accuracy, test controlled PPO updates, then compare 64×64 versus 128×128 actors.
The full [results and protocol](../../reports/training-strategy-20261016/report.md)
include every update, per-agent evaluations, initial baselines and calibration.

## Findings

- The current .001 actor update produced mean joint Gaussian KL .094–.210
  on its collected states. The chosen experimental PPO bound was .01.
- The old final critic had held-out explained variance .992 on its saved
  objective despite its actor having zero measured balanced time. Predicting
  the returns of a failing policy is different from learning recovery control.
- Fresh critics fitted once per batch remained weak in these three-batch pilots.
  A separate frozen-data diagnostic improved held-out explained variance from
  −.106 after one fit to .836 after 100 fits, with the same 64×64 critic and
  unchanged actor. This is evidence about fitting sampled returns, not proof
  that more critic fitting will improve the actor.
- Smaller-step MC reduced policy change substantially. Starting from the same
  saved best actor, stochastic balanced duration changed from .615 s to .481 s
  for the original method, .531 s for MC at .0001, and .463 s for PPO at .0001.
  PPO did not outperform the smaller-step control in this comparison.
- Fresh 64×64 actors averaged .254→.276 s stochastic balanced time; fresh
  128×128 actors averaged .141→.181 s. Both improved on two of three seeds.
  The larger width gained more on average, primarily through seed 44, but
  started lower and finished lower. Different initial functions and zero-duration
  floor effects prevent a strong causal capacity conclusion from three seeds.
- Every evaluated episode still ended in a fall. No robust balance or reliable
  general learning improvement has been established.

## Implementation

`python/ppo_experiment.py` provides experimental PPO-Clip with fixed exploration,
normalized Monte Carlo advantages, eight maximum full-batch actor optimizer
steps, and an exact empirical mean-KL guard. Overshoots restore both weights
and Adam state. Actor and critic optimizers remain separate. No GAE or entropy
schedule was added. Critic fitting remained one step per batch in the matched
capacity experiment. The initially rejected .001 PPO attempt is preserved in
the report; the .0001 rate was selected from update-size evidence and tested
with its own MC control.

`python/arena_env.py` optionally records detached latent Gaussian samples and
old means when `record_policy=True`. The default remains false. Recorded and
ordinary physical rollouts produced identical states, returns and metrics in
an independent matched four-agent check. Raw samples, rather than clipped motor
targets, are required for correct probability ratios.

`python/training_strategy_experiment.py` runs the bounded comparisons;
`python/experiments/training_strategy_report.py` exports the tables/CSV;
`python/experiments/critic_fit_curve.py` runs frozen-actor value fitting. Experimental weights
are explicitly tagged non-production-resumable and stored under `reports/`.

Nine planned runs each used 32 episodes per batch and three batches. The capacity
comparison held the critic at 64×64, preserving Tanh activations, 205 inputs,
32 actions, reward, physics, exploration, world spacing, rates and evaluation
streams. Matched actor seeds were 42, 43 and 44. Initial weights necessarily
differ across widths; changes are reported against each actor's own baseline.
Four agents in one arena are coupled samples, not four independent training runs.

83 focused Python tests passed, including PPO clipping, raw-action alignment,
on-policy freshness, exact KL, rollback of existing Adam momentum, deterministic
replay, checkpoint compatibility and existing actor–critic behavior. A source
and artifact hash manifest accompanies the report. Both original model hashes
remain unchanged. No production trainer, CLI defaults, network defaults,
physics, reward or termination behavior was replaced by these experiments.

## Decision

Keep the default actor at 64×64 and do not promote this PPO pilot to production
yet. The next controlled experiment should vary critic fitting effort while
retaining one actor architecture, fixed update limits and held-out evaluation.
Do not assume either a larger network or a KL limit guarantees learning.

Reproduction commands and full measurements are in the linked report. Use a
new output directory. `--continue` can retain completed branches from an
interrupted experiment with matching source/physics/protocol inputs; it restarts
incomplete branches rather than treating partial updates as final results.
