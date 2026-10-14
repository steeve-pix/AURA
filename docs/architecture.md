# Current AURA application

AURA has one application physics configuration. The visual app always enables
all 105 eligible self-collision pairs; directly joined parts are excluded.
CLion Control+R and the terminal launch the same executable without arguments:

```sh
cmake --build build --target aura
./build/AURA.app/Contents/MacOS/AURA
```

## Where to make changes

| Responsibility | Entry point |
| --- | --- |
| Window, input, camera, frame coordination | `src/main.cpp` |
| Ordered joints, enabled motors, marker sizes | `src/app/BodyJoint.cpp` |
| One fixed application physics tick | `src/app/SimulationStep.cpp` |
| Current joint correction sequence | `src/body/JointSolver.cpp` |
| Component/contact pose feasibility | `src/body/ComponentPosition.cpp`, `src/body/BodySelfCollision.cpp` |
| Body shapes and physical contact impulses | `src/physics/CollisionShape.cpp` |
| Body meshes and read-only drawing | `src/render/BodyRenderer.cpp` |
| Runtime summaries and measurement history | `src/app/BodyDiagnostics.hpp` |
| Historical failure replays and experimental modes | `tests/full_body_diagnostics.cpp` |

## A physics tick

1. Apply the enabled joint motors and gravity; integrate at 120 Hz.
2. Repeat 16 times: floor response, forward joints, backward joints,
   body self-collision, then room containment/contact response.
3. Clear forces and torques. Record completed-step measurements.

The backward sweep starts at the penultimate joint, preserving the established
ordering. Each joint projects its hinge axis and angular limits, repairs its
attachment through connected components, then runs four local velocity passes.
Each velocity pass solves anchor motion, hinge transverse motion, then outward
angular-limit motion.

**Pose projections change poses only. Physical impulses change velocities.**
The body-specific angular helpers retain their established pivot and floor
policies. The new joint entry point consolidates orchestration rather than
replacing those numerical algorithms.

## Verify a change

```sh
cmake --build build --target aura_runtime_baseline_tests
ctest --test-dir build -R '^runtime_baseline$' --output-on-failure
```

This gate runs the actual application tick for ten seconds without OpenGL.
It checks finite states, bounded positions, attachment/angle limits, floor/room
containment, and body penetration using the existing tolerances. It uses explicit
checks in optimized builds too. Its summary output can be compared with visual
application logs to detect trajectory changes.

The `aura_simulation` CMake library shares current stepping between this gate and
the app. Historical diagnostics keep their own configuration and implementation
so old failure fixtures remain reproducible. See [physics-history.md](physics-history.md)
when investigating those experiments, rather than selecting another app mode.

This is a verified ten-second collision baseline. Anatomical realism, sustained
balance and arbitrary-scene stability remain future work. The Python motor and
observation contracts are described below.

## Cleanup verification

The initial cleanup passed 12 focused Debug tests, including the application
baseline. All ten one-second summaries from both the headless gate and rebuilt
visual application match the pre-cleanup run. Maximum joint gap was 4.78e-7,
maximum body penetration 0.000499725, room penetration zero, and maximum body
radius 7.83932 over ten seconds. No physical parameters were changed.


## Neural motor contract (C++ action schema 2, policy action schema 3)

The observation contract remains schema 2 with 205 values, unchanged by the
motor expansion. The current neural policy uses `205 → 64 → 64 → 14` with Tanh activations and has its
own action-schema version 3. The C++ simulator still exposes 15 joint targets
under C++ action schema 2; the neural policy omits the disabled head motor.
`ActionSpec.hpp` is the authoritative C++ action order, member-to-joint mapping,
and physical-limit contract. Construction checks the limits against the actual
skeleton. Bindings, viewer JSON and target clamping iterate that contract.

Every motor commands **one twist angle** around a parent-local hinge axis.
Both elbows use **−X**, so positive flexion bends hanging forearms toward the
feet's forward direction (+Z). Other joints retain +X; positive knee flexion
bends the shin backward. Elbow limits remain 0…2.4 rad and gains are unchanged.
The motor
applies equal and opposite torques to parent and child:
`torque = stiffness × (target − angle) − damping × relative angular speed`.
The hips additionally permit passive Y/Z swing of ±0.4 rad; no new swing actions
are introduced. Motor gains, gravity, timestep, solver iterations and collision
response are unchanged. New targets default to zero; a neutral enabled motor
still resists motion through stiffness and damping.

| Index | Action key | Joint: parent → child | Physical range (rad) | Neural range (rad) | Stiffness / damping | Default motor |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | left_hip_target | pelvis → left_thigh | −0.8…0.8 | −0.2…0.2 | 15 / 4 | on |
| 1 | right_hip_target | pelvis → right_thigh | −0.8…0.8 | −0.2…0.2 | 15 / 4 | on |
| 2 | left_knee_target | left_thigh → left_shin | 0…2.2 | 0…0.3 | 10 / 4 | on |
| 3 | right_knee_target | right_thigh → right_shin | 0…2.2 | 0…0.3 | 10 / 4 | on |
| 4 | left_ankle_target | left_shin → left_foot | −0.35…0.85 | −0.2…0.2 | 10 / 2 | on |
| 5 | right_ankle_target | right_shin → right_foot | −0.35…0.85 | −0.2…0.2 | 10 / 2 | on |
| 6 | waist_target | torso → pelvis | −0.5…0.5 | −0.1…0.1 | 8 / 3 | on |
| 7 | neck_target | torso → neck | −0.5…0.5 | −0.1…0.1 | 6 / 2.5 | on |
| 8 | head_target | neck → head | −0.5…0.5 | — | 10 / 2 | **off: excluded from policy** |
| 9 | left_shoulder_target | torso → left_upper_arm | −1.5…1.5 | −0.3…0.3 | 10 / 2 | on |
| 10 | right_shoulder_target | torso → right_upper_arm | −1.5…1.5 | −0.3…0.3 | 10 / 2 | on |
| 11 | left_elbow_target | left_upper_arm → left_forearm | 0…2.4 | 0…0.48 | 6 / 2.5 | on |
| 12 | right_elbow_target | right_upper_arm → right_forearm | 0…2.4 | 0…0.48 | 6 / 2.5 | on |
| 13 | left_wrist_target | left_forearm → left_hand | −1.2…1.2 | −0.24…0.24 | 4 / 2 | on |
| 14 | right_wrist_target | right_forearm → right_hand | −1.2…1.2 | −0.24…0.24 | 4 / 2 | on |

The 14 neural outputs follow the C++ order with the head entry omitted: C++ action
indices 0–7 map to policy outputs 0–7, and C++ indices 9–14 map to policy outputs
8–13. The original six neural ranges are preserved. New ranges use one fifth of each
joint's positive physical excursion, symmetrically for signed joints. Elbows and
knees retain one-sided conversion: negative normalized output maps to zero.
Normalized action conversion is bounded to [−1,1] and rejects non-finite values.
The reinforcement-learning objective and sampling implementation are unchanged.

The forward-elbow correction changes physical behavior without changing tensor
sizes or action ordering. Historical checkpoints remain loadable but need fresh
evaluation under this skeleton; earlier learning results describe the earlier
physics. Archived contact-pose replays explicitly retain their original +X elbow
axes. Neither policy weights nor historical experiment artifacts are migrated.

Joint direction audit (relative to neutral posture; +Z forward, +Y up):

| Joint group | Positive target | Negative target |
| --- | --- | --- |
| Elbows | Forearm bends forward | Blocked at straight (0) |
| Knees | Shin bends backward | Blocked at straight (0) |
| Shoulders | Hanging arm moves backward | Arm moves forward |
| Hips | Thigh moves backward | Thigh moves forward |
| Ankles | Toes down (plantarflexion) | Toes up (dorsiflexion) |
| Wrists | Hanging hand tilts backward | Hand tilts forward |
| Neck and head | Upper segment nods forward | Upper segment tilts backward |
| Waist | Torso tilts backward relative to pelvis | Torso tilts forward relative to pelvis |

Waist storage is torso → pelvis, so interpreting torso motion requires the
inverse relative rotation. Wrist meshes have no palm/thumb convention; their
pitch can be tested, but anatomical palm-side flexion cannot be established.
These are limited pitch controls, not complete human joint models. Hips also
retain bounded passive swing. The head motor remains disabled by default due to
the documented head/neck coupling issue; its independent direction test enables
it with the neck motor disabled. `joint_directions` checks all 15 joints in both
identity and tilted parent frames, torque/inertia response, damping, limits and
hinge swing rejection. `runtime_baseline` checks signed command response for all
15 motors in paired full-body simulations, then runs the default ten-second gate.

`python.residual_critic_experiment` is an offline diagnostic, separate from actor
training. It fits a training-only timestep lookup, then trains a fresh `AuraValue`
on `G/100 - b_t`; held-out predictions are `b_t + V(s)`. Missing lookup timesteps
use the training mean, as in Step 113. Its checkpoints have kind
`aura_value_residual` and include the fixed lookup and return scale, so they cannot
be mistaken for ordinary absolute-return critics. Step 114 uses the archived
pre-elbow-correction datasets, validates their hashes and original critic MSE,
and reports results at 0, 1, 5, 10 and 20 updates. It constructs no environment,
collects no trajectories, and never changes production REINFORCE or actor weights.
The archived-data findings do not describe the corrected skeleton's performance.

`sim.set_action({...})` clamps targets without stepping. `sim.step_action({...})`
clamps and advances one tick. Partial dictionaries preserve omitted targets;
unknown keys and non-finite targets are rejected without applying a partial
update. `sim.action()` reports all 15 clamped targets. Existing six-key callers
remain valid. Motor selection is explicit through
`sim.set_motor_enabled(joint_name, enabled)` and `sim.motor_states()`; selection
survives reset, while action targets reset to neutral.

### Supported motor configuration and head/neck blocker

There are 15 independently addressable targets and **14 motors enabled by
default**. Head and neck motors each respond correctly in isolation, but enabling
both with their existing gains causes large oscillation. Starting from the same
pose, a 0.1-rad command with both active produced relative angular speeds up to
32.64 rad/s for neck and 54.78 rad/s for head over 60 ticks; strong oscillation
already appeared at tick 8, before landing. With either motor active alone, the
corresponding maximum was approximately 1.02 or 1.29 rad/s. This implicates the
coupled explicitly integrated motor response around the low-inertia neck body;
it is not a support-contact spike. We have not changed gains or integration to
hide the issue. Head is opt-in, and safe head-control tests disable the neck motor.
Simultaneous 15-motor control is **not certified for training**.

The current 14-output policy excludes `head_target`; C++ still supports setting it
for non-policy callers. Historical 15-output checkpoints remain unchanged and
are incompatible with policy action schema 3. They must be evaluated or resumed
only with their matching historical interface; the current loader rejects them.

### Deterministic, untrained baselines (2026-10-13)

Release build; fixed 120-Hz simulation; default reset; all targets zero for neutral
runs; PyTorch seed 42 and no exploration for untrained-policy runs. No training
or historical checkpoint writes were performed. Every run ended with `fallen`,
triggered by pelvis height dropping below half of its initial height.

| Controller / motor configuration | Steps | Time (s) | Final pelvis uprightness | Final support margin |
| --- | --- | --- | --- | --- |
| Neutral, original six motors | 317 | 2.641667 | 0.965421 | −1.522397 |
| Neutral, default 14 motors (head off) | 379 | 3.158333 | 0.992871 | N/A: invalid polygon |
| Untrained 15-output policy, default 14 motors | 301 | 2.508333 | 0.989155 | N/A: invalid polygon |
| Neutral, experimental all 15 motors | 370 | 3.083333 | 0.789225 | −2.818413 |
| Untrained 15-output policy, experimental all 15 motors | 324 | 2.700000 | 0.984535 | N/A: invalid polygon |

The six-motor result reproduces the previous 317-step baseline in the same new
binary with upper-body motors explicitly disabled. Differences measure motor
configuration, not learning. An invalid polygon's stored zero margin is not a
zero-distance boundary measurement. The all-15 rows are diagnostic comparisons
and do not remove the head/neck control blocker.

### Compatibility and verification

Checkpoints retain separate observation_schema_version and action_schema_version.
The loader rejects six-output/legacy action contracts and reordered action keys;
no partial weight migration or historical-file overwrite is performed.

C++ regression tests check all mappings and physical limits, transactional invalid
actions, preserved motor selections, equal/opposite motor torques, positive and
negative responses, and twist constraints. New-motor response tests compare two
full-body simulations for 24 ticks; the head test explicitly disables neck.
The ten-second application gate passes with maximum anchor gap 9.56e−7,
penetration 0.000980496, no floor/room penetration and maximum radius 8.21261.
That is a finite-state/constraint result, not ten seconds of standing.

The full 51-test C++ suite has one separate failure in the unchanged `joint`
isolated four-part legacy replay: ankle gaps at ticks 427, 428 and 687 exceed
0.001, with maximum 0.00116092. That target does not link the new action or motor
configuration code. Its solver and thresholds were left intact. Python action,
observation, evaluation and checkpoint tests pass. Before training, address the
head/neck coupling, review that existing attachment failure, and choose the active
motor configuration explicitly. The custom torque motor has no actuator torque
saturation, and bounded action targets do not guarantee stable target tracking.


Verification commands and final results for this change:

- `cmake --build build-release -j 8`: passed (application, Python module, tests and diagnostics).
- `ctest --test-dir build-release --output-on-failure -j 8`: 50/51 passed;
  only the separate legacy `joint` ankle-attachment replay failed as described above.
- `PYTHONPATH=build-release:. python3 -m unittest discover -s tests -p 'test_*.py'`:
  33/33 passed. These include observation, action, checkpoint, evaluation, worker
  and viewer tests. Temporary optimizer tests do not start a model-training run.
- `cmake --build cli/build -j 6` and `ctest --test-dir cli/build --output-on-failure`:
  build passed; 4/4 screen tests passed. The control table follows selection when
  15 rows do not fit, and the training-screen fixture now exercises 205 inputs
  and 14 outputs.
- Python compilation checks and `git diff --check`: passed.
- `python/aura_env.py` SHA-256 stayed
  `35787981490557b0bc6549bdfbeb0892e381d0bb1d33372a2aa13ec80c83303e`.
  No observation fields, ordering, frame calculations or normalization changed.

Files touched by the motor-control task (other existing working-tree changes are
outside this list):

- C++ contract/bridge: `include/aura/sim/ActionSpec.hpp`,
  `include/aura/sim/AuraSimulation.hpp`, `src/sim/AuraSimulation.cpp`,
  `src/app/BodyJoint.cpp`, `src/python/AuraPython.cpp`, `src/main.cpp`.
- Python policy/checkpoints: `python/aura_policy.py`, `python/model_checkpoint.py`;
  `python/control_aura.py` help text.
- CLI: `cli/main.cpp`, `cli/ControlScreen.cpp`, `cli/README.md`.
- Tests: `tests/runtime_baseline_tests.cpp`, `tests/test_nn_configuration.py`,
  `tests/test_model_checkpoint.py`, `tests/test_best_checkpoint.py`,
  `tests/test_evaluation.py`, `tests/control_screen_tests.cpp`,
  `tests/nn_training_screen_tests.cpp`.
- Documentation: `docs/architecture.md`.

No physical gain, joint limit, integration rule, force/torque calculation,
reinforcement-learning objective or hidden-layer architecture was changed.
Historical model files were not written, and no commit was created.
