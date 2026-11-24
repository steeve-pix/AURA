# Reward repair and bounded validation — 16 October 2026

The objective mismatch is corrected; improved learning is **not** demonstrated.
The original completed model and best checkpoint are unchanged. No extended
training run was launched or interrupted. The existing waiting worker was left
alone; loaded workers retain their old implementation until restarted.

## Evidence and changes

The completed 1000-update actor has zero measured deterministic and stochastic
balanced duration, versus approximately .825 s / .6153 s for its best saved actor
(update 127). Reward-component diagnostics found that the old quality objective
exempted UNBALANCED states from stationary-hold and planted-foot-slip costs.
Most high-quality UNBALANCED measurements also violated the angular-speed entry
criterion. Neither finding proves intentional exploitation by the learned actor.
The support factor's .25 floor is not a .25 floor on the final product reward.

Quality version 3 makes hold/slip costs independent of classifier labels. Small
corrections remain inside the existing deadbands; moving a lifted foot does not
count as planted-foot slip. Rotation uses a .25 rad/s deadband and .315 rad/s
control scale: both pelvis and torso at .4 rad/s score about .80, at .65 rad/s
about .20, and at 1 rad/s below .005 for this factor. These targets anchor a
continuous standing-task objective to the existing classifier envelope. They
are design choices, not empirically validated recovery limits. The historical
angular_dynamic_fraction metadata field is retained; angular_control_scale now
controls rotation. Posture, support floor, height, and other scales are unchanged.

Nonterminal reward remains the product of seven physical factors in [0,1].
Confirmed FALLEN yields zero and terminates; future failure utility is zero.
Truncation retains the final physical reward with finite-horizon zero
continuation. No negative terminal penalty or experimental absorbing penalty is
added. Actor advantages and critic targets still use gamma=.995 and critic
return scale=100. The maximum discounted quality return remains below 200.

No physics, classifier thresholds, termination rules, neural inputs (205), motor
outputs (32), network layers/activations, learning rate or update algorithm changed.

## Compatibility and CLI

Environment version 5 / reward version 3 / termination version 2 / **quality
version 3** / ground version 3 is the active contract. Checkpoint format remains 1.
Quality version 2 is retained exactly in python/balance_quality_v2.py. Readers
select the saved known quality version and still validate every contract field.
Unknown versions and changed physics cannot silently fall back.

Old quality actors are labeled historical. Watch, Evaluate, Compare and Diagnose
remain available using their saved objective. Resume remains strict. `/train` →
**5 Fork actor** accepts compatible or historical actors, including a best copy,
and asks for a new name. It copies the exact actor weights and saved world,
initializes a fresh critic and both Adam optimizers, resets counters/history,
and records source filename/SHA-256/reward version. It never overwrites the source.
New-model and existing-model resume behavior otherwise stays unchanged.

## Paired physical pilot

Starting actor: 4ur0r4_balance_00_best.pt, update 127.
Source SHA-256: 1a3bef8470241ede99298623c8348ce9c5d635ae3898f79db01dbdef2de4f4b2.
Two branches: old quality 2 and corrected quality 3, identical actor weights and
fresh critics/Adam in each. Three updates, 32 episodes/update, four agents,
12 m spacing, collisions enabled, CPU, Adam .001. Each branch collects 96 training
episodes. Exploration seeds 290042–290044 are matched by update. Evaluation after
every update uses four deterministic agents and twelve stochastic episodes
(three streams 190042–190044 × four agents). One CPU tensor thread is used for
this bounded experiment; physics settings are unchanged.

| Update | Old deterministic balance (s) | New deterministic balance (s) | Old stochastic balance (s) | New stochastic balance (s) |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0.8250 | 0.8250 | 0.6153 | 0.6153 |
| 1 | 0.8167 | 0.8167 | 0.5521 | 0.5264 |
| 2 | 0.6333 | 0.7000 | 0.6465 | 0.5625 |
| 3 | 0.4917 | 0.6667 | 0.5111 | 0.5875 |

Both branches regress relative to the saved starting actor. The corrected reward
reduces the final regression on this pilot, but neither branch demonstrates an
improvement. All evaluated episodes fall. One actor and three updates cannot
establish statistical superiority or generalization. Returns under different
objectives must not be compared as if they used one scale.

Full per-update/per-agent results and isolated pilot checkpoints are retained in
reports/reward-repair-pilot-20261016/. No production checkpoints were migrated.
Reproduce with an unused output directory:

```sh
PYTHONPATH=build-release:. python3 -m python.experiments.reward_repair_pilot \
  models/4ur0r4_balance_00_best.pt reports/reward-repair-another-run
```

## Verification and limits

73 focused Python tests pass: quality semantics, historical behavior, shared
collection, checkpoint contracts, fork identity/source preservation, separate
actor/critic gradients, resume/Adam/RNG, GPU paths, metrics, and viewer behavior.
Eight CLI suites cover layouts and keyboard handling. A separate Fork actor
keyboard smoke test exercises model selection, name entry and settings without
training or writing models. Layout was visually inspected at 80 and 120 columns.
Debug/Release behavioral termination/classifier checks and strict historical
Release replay remain intact, as do ground-motion tests.

Remaining risks: a supported tilted state may still accumulate small positive
reward; multiplicative quality can be sparse; stronger rotation costs may
undervalue aggressive but successful recovery. The current batch estimator and
unconstrained Adam updates can still regress. No optimizer fault or sole cause of
the long-run regression has been established. Further automatic long training
is not justified by this pilot. The next bounded investigation should measure
policy change (Gaussian KL), critic held-out error/explained variance and matched
physical outcomes before changing update size or introducing a recovery
curriculum. Keep this reward version identifiable while doing that comparison.

## Files changed in this repair

- python/balance_quality.py; python/balance_quality_v2.py
- python/aura_env.py; python/arena_env.py; python/model_checkpoint.py
- python/diagnostics/reward_component_audit.py; python/experiments/reward_repair_pilot.py
- cli/backend/training_run.py; cli/backend/nn_session.py
- cli/backend/model_viewer.py; cli/backend/evaluate_model.py; cli/backend/model_tools.py
- cli/screens/TrainingSetup.cpp; cli/terminal/Terminal.cpp; cli/screens/ModelsScreen.cpp
- cli/screens/NNTraining.cpp; cli/app/main.cpp; cli/README.md
- tests/learning/test_balance_quality.py; tests/learning/test_reward_component_audit.py
- tests/learning/test_balance_system.py; tests/learning/test_arena_training.py
- tests/learning/test_reward_repair.py; tests/cli/models_screen_tests.cpp
- tests/cli/cli_fork_smoke.py; this report
