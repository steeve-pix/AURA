# AURA development checkpoint — 2026-10-13

This checkpoint preserves the current simulation, neural pipeline, research
results, and CLI before expanding the body's movement capabilities.

## Git organization

Work is grouped on `codex/aura-foundations-checkpoint`, based on
`feature/minimal-restart` at `1d4d0bf`. The integration uses an explicit
`--no-ff` merge into `feature/minimal-restart`; existing history is not rewritten.
The commits separate simulation/physics, Python learning and diagnostics,
interactive CLI, and architecture/research documentation.

## Current contracts and limitations

- Observation schema 2: 205 fixed, finite inputs with documented frames/scales.
- Policy action schema 3: 14 outputs; physical action schema 2: 15 targets.
- Actor: 205 → 64 → 64 → 14. Independent critic: 205 → 64 → 64 → 1.
- Default configuration: 14 enabled motors; head disabled because simultaneous
  original-gain head/neck operation remains unstable. Independent head movement
  is tested with the neck motor disabled.
- Positive elbow flexion now bends forward. Other joint directions, signed
  commands, bounds, torque reactions, and rotated frames are audited.
- Controls are simplified pitch joints. Hips additionally allow bounded passive
  swing. There is no full shoulder articulation, active hip yaw/roll, articulated
  spine, forearm pronation, or articulated toes.
- Production actor learning remains REINFORCE with a leave-one-out timestep
  baseline. Critic and residual-critic experiments are independent diagnostics.

## Verification at the checkpoint

- Release simulation/viewer/Python extension build: successful.
- C++: 51/52 tests pass, including `joint_directions` and the ten-second
  `runtime_baseline` application gate. The legacy `joint` test still fails its
  ankle attachment check. This failure was reproduced before the elbow change;
  its tolerance was not increased. This is a known failing suite, not an
  entirely green baseline.
- Python: 62 unit tests pass; compilation checks pass.
- CLI: all 6 registered suites pass, including production keyboard handlers
  exercised against memory-only controls and a fake worker.
- Additional real-terminal checks pass for model-library evaluation, file
  filters, immediate command cancellation/activity, and transcript scrolling.
  The evaluation smoke test compares with the current physics build instead
  of assuming the historical 377-step score.
- Whitespace/diff checks: clean.
- No new training experiment was run to prepare this Git checkpoint.

## Historical research and local artifacts

The saved Step 110–114 reports describe archived pre-elbow-correction physics.
They remain historical results, not measurements of the corrected skeleton.
Step 114's residual critic improved held-out MSE from 0.007669 to 0.006606 on
those historical trajectories, but has not demonstrated better policy gradients
or balance. The production actor has not adopted it.

Model weights, recovered training/validation tensor datasets, runtime viewer JSON,
build products, and caches remain local and ignored. They are preserved on disk,
not embedded in these commits. A fresh clone can inspect the reports but cannot
reproduce archive-dependent experiments without those separately preserved
artifacts. Existing checkpoints may still load after the elbow correction, but
must be reevaluated before comparing performance or resuming experiments.

## Next body revision: human-like robot

The requested direction is a human-like robot, not a full biological replica.
Desired capabilities include forward and sideways arm raising, bringing hands
together to clap, crossing legs, and turning/twisting the body toward a spin.
These are future capabilities, not features of this checkpoint.

The next implementation must explicitly define the additional controlled axes,
direction-dependent limits, solver support, collision behavior, action contract,
and observation coverage. New degrees of freedom require fresh physical and
learning baselines; existing experiments must not silently be presented as
comparable. Keep the custom C++ physics / Python intelligence boundary and avoid
introducing anatomical complexity without a useful movement requirement.
