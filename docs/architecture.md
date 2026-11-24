# Current AURA application

The [test directory guide](../tests/README.md) separates current subsystem checks,
historical replays, shared fixtures, and diagnostic tools, with commands for each.

AURA has one application physics configuration. The visual app always enables
all 105 eligible self-collision pairs; directly joined parts are excluded.
CLion Control+R and the terminal launch the same executable without arguments:

```sh
cmake --build build --target aura
./build/AURA.app/Contents/MacOS/AURA
```

## Where to make changes

| Responsibility                                    | Entry point                                                        |
|---------------------------------------------------|--------------------------------------------------------------------|
| Window, input, camera, frame coordination         | `src/main.cpp`                                                     |
| Ordered joints, enabled motors, marker sizes      | `src/app/BodyJoint.cpp`                                            |
| One fixed application physics tick                | `src/app/SimulationStep.cpp`                                       |
| Current joint correction sequence                 | `src/body/JointSolver.cpp`                                         |
| Component/contact pose feasibility                | `src/body/ComponentPosition.cpp`, `src/body/BodySelfCollision.cpp` |
| Body shapes and physical contact impulses         | `src/physics/CollisionShape.cpp`                                   |
| Body meshes and read-only drawing                 | `src/render/BodyRenderer.cpp`                                      |
| Runtime summaries and measurement history         | `src/app/BodyDiagnostics.hpp`                                      |
| Historical failure replays and experimental modes | `tests/historical/full_body_diagnostics.cpp`                                  |

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

## Human-like robot articulation and neural contract

The body retains 16 rigid parts and 15 physical joints. It now exposes **33 target
coordinates**, with **14 joint motors enabled** by default. A multi-coordinate
joint has one motor-enable switch controlling all of its coordinates.

| Joint group       | Coordinates             | Physical range, radians                                                              | Direction convention                                                                          |
|-------------------|-------------------------|--------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------|
| Shoulders         | pitch, swing Y, swing Z | pitch −3…0.8; Y ±0.8; left Z −1.8…0.2, right Z −0.2…1.8                              | negative pitch forward; mirrored sideways lift                                                |
| Hips              | pitch, swing Y, swing Z | pitch −1.8…0.5; left Y −0.9…0.6, right Y −0.6…0.9; left Z −0.9…0.5, right Z −0.5…0.9 | negative pitch forward; left negative/right positive Z outward; the reverse is across midline |
| Waist             | pitch, swing Y, swing Z | pitch ±0.5; Y ±1; Z ±0.35                                                            | stored torso→pelvis: positive Y turns torso left; positive Z bends torso right                |
| Neck              | pitch, swing Y, swing Z | pitch ±0.5; Y ±0.9; Z ±0.3                                                           | positive pitch nods forward; negative Y looks left; positive Z tilts head left                |
| Wrists            | pitch, swing Y, swing Z | pitch ±1.2; Y ±1.2; Z ±0.35                                                          | hanging hand: negative pitch forward; Y rotates its length; positive Z moves right            |
| Ankles            | pitch, swing Z          | pitch −0.35…0.85; Z ±0.3; Y locked at zero                                           | negative pitch toes up; positive toes down; Z tilts sideways                                  |
| Elbows            | hinge                   | 0…2.4 about parent-local −X                                                          | positive flexion brings hanging forearm forward                                               |
| Knees             | hinge                   | 0…2.2 about parent-local +X                                                          | positive flexion bends shin backward                                                          |
| Separate head nod | hinge, motor off        | ±0.5 about parent-local +X                                                           | neck already controls head orientation                                                        |

These are bounded robot joints, not a biological replica. Wrist rotation stands
in for hand orientation without adding a separate forearm bone. There are no
fingers, toes, scapulae or anatomical coupled-limit surfaces. The meshes have no
palm/thumb convention. Cross-midline leg movement is permitted where actual
self-collision leaves a path; no clapping, leg-crossing or spinning gesture is
scripted or certified. Whole-body rotation must emerge through physical torque
and ground contact rather than changing a render transform.

Relative orientation for multi-coordinate joints is `swing(Y,Z) * twist(X)`.
Swing Y/Z are components of the shortest rotation vector in the parent frame, **not Euler angles**. Combined coordinates
therefore depend on posture.
`ActionSpec.hpp` owns action keys, order, coordinate mapping, limits and direction
hints; `ActionGeometry.hpp` selects the matching measured coordinate. The first
19 keys retain their order. Fourteen new keys append hip Y/Z, waist Y/Z, neck Y/Z,
wrist Y/Z and ankle Z targets. Inputs and output targets remain in radians.

Multi-coordinate motors use the actual coordinate gradients to map spring errors
and coordinate velocities into equal/opposite world torque. Damping power is
`−D × sum(coordinate_rate²)`. Signed pitch error follows the permitted interval
rather than wrapping across a stop. Existing gains remain unchanged. Motor work
uses torque dot relative angular velocity. All other hinge motors retain their
previous scalar torque calculation.

Geometry corrections rotate the complete graph-derived child component around
its anchor and preserve descendant relationships. Hip floor handling retains its
existing whole-body lift policy; wrist, waist and ankle corrections defer floor
containment to the existing application floor/room passes. Velocity corrections
remain local endpoint impulses without copying descendant velocities. A small
active-set solve handles up to three simultaneous velocity constraints, including
locked ankle yaw, so one stop cannot reintroduce velocity through another. The
120-Hz timestep, gains, solver sweeps, iteration counts and collision budgets are
unchanged. Collision projections still check every joint's directional bounds.

### Manual controls

`/control` defaults to requested movement reach (0–100%). It offers arm, leg,
hip-turn, torso-turn/side-bend, head-turn, wrist and ankle movements. Arrows reach
the entire scrolling list; number keys select the first nine. Left/Right adjusts
reach by 5%, Enter requests full reach, A opens all 33 individual angle controls,
M returns to movements, Tab switches views, and R requests neutral targets.
Presets change only their owned target coordinates. Arm presets also straighten
and reset that arm's wrist, including its new orientation coordinates. Reach
percent is a target interpolation, not a measurement of achieved pose or balance.

`sim.set_action({...})` clamps without stepping; `sim.step_action({...})` clamps
and advances one tick. Partial dictionaries retain omitted targets; non-finite
values and unknown keys reject the entire request. `sim.action()` reports all 33
coordinates. Existing six-key callers remain accepted. Reset neutralizes action
targets while preserving per-joint motor selection.

### Actor and critic

The observation interface remains **schema 2, 205 inputs**. Its existing joint
quaternions and parent-local angular velocities already describe every added
rotation. No sensors, frames, normalization, physics observations or rewards
were added.

The actor is **205 → 64 Tanh → 64 Tanh → 32 Tanh**. It controls every exposed
coordinate except `head_target`. C++ physical action schema is **4** and neural
policy action schema is **5**. Original controlled keys retain their relative
order; new outputs follow the physical contract. `ACTION_TRANSFORMS` keeps the
original learning ranges and adds conservative signed ranges for the newly
actuated coordinates. These exploration ranges are intentionally smaller than
full manual joint limits. REINFORCE, leave-one-out advantages, Adam and the reward
function are unchanged. No learning improvement is claimed by this body revision.

The critic remains **205 → 64 Tanh → 64 Tanh → 1 linear value**, predicting returns
scaled by 100. It is independent of production actor updates. Critic-only mode
freezes the actor, uses separate episode datasets, trains a fresh critic and
compares held-out error against the training-mean constant predictor. New critic
artifacts record the policy and physical action schemas. **Earlier critic quality
results describe the earlier body and actor, not this revision.** A fresh
validation experiment is required before assessing prediction quality here.

Old actor/critic artifacts are preserved. Actor schema 3/4 checkpoints cannot be
silently resumed with 32 outputs; use matching historical code for old results,
or create a fresh model for this body. No weights, optimizer state or historical
experiment datasets are migrated. Offline residual-critic diagnostics retain
schema guards so old datasets cannot be mistaken for current-body evidence.

### Supported head configuration and historical replays

The separate head nod motor remains disabled. Earlier tests found large
oscillation when its motor and the neck motor ran together at their existing
gains. Neck articulation now supplies head orientation in all three directions;
this does not resolve or certify simultaneous head/neck motor activation. The
actor therefore omits the head target. Isolated head-control tests disable the
neck motor, as before.

`tests/historical/LegacyArticulation.hpp` explicitly restores the prior joint configuration
for archived replay fixtures. It is not an application mode and never changes
the current simulator. Current runtime, direction, shoulder and articulation
tests use the new body. Replay profile selection preserves the meaning of stored
failures rather than applying today's joint types to historical measurements.

### Articulation revision validation

Release build, fixed 120 Hz, 16 outer iterations and four local velocity passes,
with all 105 eligible collision pairs enabled:

- The application resting-pose gate runs 1,200 ticks with its existing hip/knee
  targets (0.5/0.6 rad). Maximum attachment gap 4.92e−7, body penetration
  0.00049901, floor/room penetration zero and body-center radius 7.83932 (bound 15).
- Twelve separate 1,200-tick simulator runs cover four shoulder motions and eight
  hip/torso/neck/wrist/ankle/combined motions. Targets ramp for one second, hold
  two seconds, return over one second, then remain neutral. Maximum attachment
  gap across these runs is 1.91e−6; maximum angular-limit error 9.93e−7 rad;
  maximum body overlap 0.000500083; floor penetration zero.
- Local tests cover every multi-coordinate joint in neutral and tilted body
  frames, finite-difference coordinate derivatives, both bounds, all eight
  simultaneous-stop corners, locked ankle yaw, descendant attachment integrity,
  angular-momentum conservation and non-increasing angular kinetic energy.
- 53/54 C++ tests pass. The pre-existing legacy `joint` ankle-attachment test
  remains failing; no tolerance was relaxed. All 63 Python and 6 CLI tests pass.

These checks establish bounded mechanics for the stated configurations, not
balance, successful gesture tracking or safety for arbitrary target combinations.
Motion tests allow falling. The critic was not retrained and no production actor
optimizer update was performed for this revision.

### Historical deterministic, untrained baselines (2026-10-13, before 3D shoulders)

Release build; fixed 120-Hz simulation; default reset; all targets zero for neutral
runs; PyTorch seed 42 and no exploration for untrained-policy runs. No training
or historical checkpoint writes were performed. Every run ended with `fallen`,
triggered by pelvis height dropping below half of its initial height.

| Controller / motor configuration                       | Steps | Time (s) | Final pelvis uprightness | Final support margin |
|--------------------------------------------------------|-------|----------|--------------------------|----------------------|
| Neutral, original six motors                           | 317   | 2.641667 | 0.965421                 | −1.522397            |
| Neutral, default 14 motors (head off)                  | 379   | 3.158333 | 0.992871                 | N/A: invalid polygon |
| Untrained 15-output policy, default 14 motors          | 301   | 2.508333 | 0.989155                 | N/A: invalid polygon |
| Neutral, experimental all 15 motors                    | 370   | 3.083333 | 0.789225                 | −2.818413            |
| Untrained 15-output policy, experimental all 15 motors | 324   | 2.700000 | 0.984535                 | N/A: invalid polygon |

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
- CLI: `cli/app/main.cpp`, `cli/screens/ControlScreen.cpp`, `cli/README.md`.
- Tests: `tests/simulation/runtime_baseline_tests.cpp`, `tests/learning/test_nn_configuration.py`,
  `tests/learning/test_model_checkpoint.py`, `tests/learning/test_best_checkpoint.py`,
  `tests/cli/test_evaluation.py`, `tests/cli/control_screen_tests.cpp`,
  `tests/cli/nn_training_screen_tests.cpp`.
- Documentation: `docs/architecture.md`.

No physical gain, joint limit, integration rule, force/torque calculation,
reinforcement-learning objective or hidden-layer architecture was changed.
Historical model files were not written, and no commit was created.

## Step 123: raw whole-body balance diagnostics

`AuraSimulation::observation()` now measures every part directly, without
changing its physical state. `BodyPartObservation` retains `name` and `velocity`
and adds `position`, `orientation`, `up`, `angularVelocity`, `floorShape`,
`floorClearance`, and `geometricFloorContactPoints`. Positions, up vectors and
angular velocities are world-space; quaternions retain `(w,x,y,z)` ordering.
Uprightness is `orientation.rotate({0,1,0}).y`. Heights are body-center world Y,
whereas floor clearance is the lowest point of the floor shape minus floor Y.

Python's `body_part_states` retains its existing name/velocity entries and adds:
`position`, `orientation`, `up`, `angular_velocity`, `floor_shape`,
`floor_clearance`, `geometric_floor_contact_points`, and
`geometric_floor_contact`. Top-level convenience fields are `torso_height`,
`head_height`, `torso_uprightness`, `head_uprightness`,
`torso_angular_velocity`, `head_angular_velocity`, and `center_of_mass_height`.
Existing pelvis, COM-velocity, and foot-contact fields are reused. The new
`floor_contact_diagnostics` reports floor Y, the existing 0.01 proximity band,
and `kind=end_of_step_geometric_proximity`.

`JointObservation` adds `worldAnchorA` and `worldAnchorB`; Python joint entries
add `world_anchor_a`, `world_anchor_b`, `anchor_a_floor_clearance`, and
`anchor_b_floor_clearance`. Knees are joints between thighs and shins, not
separate colliders. Use their anchor clearances and neighboring part contacts;
an anchor near the floor is not itself a measured collision or support force.

These contacts are end-of-step samples using each part's `floorShape`. They are
not a log of intermediate solver events or applied impulses. The torso's floor
shape is a sphere; the other current floor shapes are boxes, including the head.
Self-collision shapes and room box contacts are separate. Foot samples reuse
the existing support-polygon contact points. No reaction forces are exposed by
this addition. Neither the 205-input encoder (schema 2) nor the 32-action policy (schema 5) changes, and standing,
termination, reward, and physics are unchanged.

Record a new neutral episode without loading checkpoints or training:

```sh
.venv/bin/python python/diagnostics/balance_diagnostics.py --output docs/traces/another-neutral-run
```

The recorder refuses to overwrite an archived trace. The baseline is
`docs/traces/step123-neutral/trace.csv`, with configuration, source hashes and
endpoint measurements in `summary.json`. It contains reset plus all 462 ticks
through current termination (3.85 seconds). Every old raw observation field and
every encoded input matched a pre-change episode exactly. Feet first enter the
geometric band at tick 42. At tick 461, standing remains true while pelvis,
torso and head tilts are about 56.14, 72.83 and 86.96 degrees, respectively;
COM vertical velocity is -8.02. Tick 462 terminates because pelvis height
falls to 2.375, below the existing 2.385 gate. Only feet register floor proximity
before termination; this episode does not measure subsequent ground collapse.

Verification: four C++ gates (new balance diagnostics, body geometry, floor
impulse audit, and the ten-second runtime baseline), three new Python diagnostic
tests, and six existing encoder/observation tests passed. Synthetic rotated and
floor-contact fixtures are diagnostic tests, not claims of full-body stability.

## Step 124: diagnostic continuation beyond the legacy episode end

The recorder defaults to stopping at the old episode boundary. Explicitly opt
into raw simulation continuation with:

```sh
.venv/bin/python python/diagnostics/balance_diagnostics.py \
  --post-termination-steps 180 \
  --output docs/traces/another-collapse-run
```

`--max-steps` optionally caps the absolute simulation step. Otherwise the cap is
the normal episode limit plus the requested continuation. After the first legacy
episode end, only this recorder calls `simulation.step_action()` directly; it
does not call `AuraEnv.step()`, calculate Python rewards, or encode neural inputs.
The unchanged zero motor targets and motor enable flags remain active. Normal
environment collection and termination are unchanged. CSV rows identify
`phase=episode` or `phase=post_termination`, preserve the current legacy predicate
and reason, and retain the first legacy end step even if the predicate changes.
Tilt angles and end-of-step mechanical extrema supplement the Step 123 fields.

Nonfinite measurements, zero orientation quaternions, invalid step progression,
or numerical exceptions stop the diagnostic. Invalid snapshots are omitted;
the summary records the error and last valid state, and the command exits with
a failure status. It never repairs the simulation. Finite anchor gaps, surface
penetration, body speeds, and quaternion norm errors are reported without
introducing new classification or solver thresholds.

The archived run is `docs/traces/step124-neutral-collapse/trace.csv`, with
configuration/source hashes in `summary.json` and event analysis in
`analysis.json`. It records steps 0 through 642, including 180 ticks after the
legacy end at 462. The old raw observations and 205 encoded inputs through that
end matched the pre-Step-123 reference exactly.

| Event                                                                  | Step | Time (seconds) |
|------------------------------------------------------------------------|-----:|---------------:|
| Legacy episode end                                                     |  462 |          3.850 |
| Left hand enters geometric floor band                                  |  482 |          4.017 |
| Torso surface reaches floor; head enters floor band                    |  514 |          4.283 |
| Head surface reaches floor (within 1e-6 numerical reporting precision) |  515 |          4.292 |
| Pelvis enters geometric floor band                                     |  599 |          4.992 |
| Diagnostic ends                                                        |  642 |          5.350 |

The contact band remains the existing 0.01 units. Torso contact appears for four
samples total (longest run three ticks); head contact for six (longest four).
These are end-of-step geometric events, not measured impact impulses. At the
final sample, pelvis/torso/head tilts are 71.43/80.49/73.23 degrees and their
center heights are 1.369/1.841/2.094. COM height is 1.187 and stored COM velocity
is approximately `(1.661, -6.496, 0.165)`. There is no re-entry into the legacy
standing predicate, and the trace does not establish settled rest or recovery.

The body continues descending after the old gate, but its positions also undergo
upward corrections: 63 post-gate transitions increase COM height while both
endpoint COM vertical velocities remain negative. The largest is +0.135922 units
from step 503 to 504, with stored vertical velocity changing from -7.507 to
-7.589. Existing pose corrections operate separately from velocity; these rises
must not be interpreted as physical rebound or successful recovery. Across this
run the maximum end-of-step anchor gap is 9.83e-7, measured floor penetration is
zero, and all recorded values are finite. Those checks do not establish physical
energy consistency; correction-driven motion remains a limitation to investigate.

Nine Python diagnostic tests passed, covering unchanged default termination,
exact Step 123 trace preservation, raw-only opt-in continuation, finite repeated
642-step traces, configured caps, and injected invalid-state/exception stops.
No C++ physics, environment reward, network, action, or classifier code changes
are part of Step 124.

## Step 125: observation-only physical balance classifier

`BalanceClassifier` (`include/aura/sim/BalanceClassifier.hpp` and
`src/sim/BalanceClassifier.cpp`) is an independent deterministic state machine.
It consumes scalar posture/support measurements and produces `Balanced`,
`Unbalanced`, or `Fallen`, exported as `BALANCED`, `UNBALANCED`, and `FALLEN`.
There is no reward, critic, action, or termination dependency. `AuraSimulation`
updates it once after each complete fixed physics tick. Observations only read
the cached assessment; direct pose edits without a tick do not advance it.
Construction/reset starts UNBALANCED with all timers zero, matching the airborne
reset configuration. FALLEN is latched until reset.

`BalanceConfig` centralizes the provisional limits. A different configuration can
be supplied to the C++ `BalanceClassifier` or `AuraSimulation` constructor.
Height references are measured from the neutral feet-on-floor body geometry
before the reset lift: pelvis 4.17, torso 6.0, mass-weighted COM approximately
3.75625. They are not fractions of the airborne reset height.

| Standing criterion                    |               Enter/re-enter BALANCED | Retain BALANCED |
|---------------------------------------|--------------------------------------:|----------------:|
| Maximum pelvis tilt                   |                            15 degrees |      20 degrees |
| Maximum torso tilt                    |                            20 degrees |      25 degrees |
| Minimum pelvis/torso/COM height ratio |                             0.80 each |       0.75 each |
| Maximum absolute COM vertical speed   |                                  0.35 |            0.60 |
| Maximum COM horizontal speed          |                                  0.35 |            0.50 |
| Maximum pelvis/torso angular speed    |                       0.40 each rad/s | 0.65 each rad/s |
| Minimum signed support margin         |                                     0 |     -0.02 units |
| Foot support                          | At least one foot; valid area polygon |            Same |

These define a near-stationary standing task, not locomotion. The tilt limits
allow modest lean while support and velocity must independently agree. Height
ratios allow moderate flexion while rejecting descent toward a collapsed body.
Speed bounds distinguish the neutral trace's roughly 0.1-unit/s settling drift
from its multi-unit/s collapse; they are initial calibration hypotheses, not
physical laws. The retention margin allowance is about 3% of the 0.65-unit foot
width. A degenerate support polygon does not qualify for controlled standing,
but it is never fall evidence by itself.

Entering requires 0.15 continuous seconds (18 ticks) inside the stricter envelope.
Leaving requires 0.05 continuous seconds (6 ticks) outside the retention envelope.
A valid sample clears the leaving timer; an invalid entry sample clears the
entry timer. Consequently isolated contact/margin changes cannot flip the state.
One-foot standing can qualify. Brief flight is tolerated; sustained flight is
UNBALANCED, not FALLEN. Re-entry requires controlled velocities as well as
posture/support, so upward positional correction alone cannot imply recovery.

Collapse evidence is either:

- **Deep-body collapse:** torso tilt at least 60 degrees, pelvis height at most
  0.35 of neutral, torso at most 0.25, and COM at most 0.30.
- **Contact corroboration:** torso tilt at least 60 degrees, pelvis at most
  0.50 of neutral, torso at most 0.35, and torso or head geometric floor contact.

The first branch requires multiple simultaneous low-height measurements with a
mostly horizontal trunk, rather than tilt alone; it does not require contact.
The second combines actual geometric proximity with low tilted posture. Head
tilt/contact alone, hands, knees, foot loss, and negative margin do not establish
collapse. Evidence must persist continuously for 0.20 seconds (24 ticks), longer
than the Step 124 torso/head contact artifacts of three/four consecutive samples.
Loss of both evidence branches clears that timer. Once FALLEN, renewed posture
or correction-induced height cannot unlatch it. This is a standing-task endpoint,
not a prediction that future recovery or standing up is impossible.

A separate `dangerous_motion` diagnostic is true at pelvis/torso tilt of at
least 45 degrees, COM downward speed of at least 2 units/s, or pelvis/torso
angular speed of at least 2 rad/s. It is a severity flag, not an extra termination
rule or an independent cause of FALLEN. Invalid numerical inputs do not provide
collapse evidence; existing encoder/diagnostic safety handling remains separate.

Python adds only raw fields: `balance_state`, `balance_state_duration` (seconds
since the latest transition, zero on its tick), `balance_conditions` and
`balance_timers`. Conditions report `supported` (geometric foot/area support),
`enter_envelope`, `stay_envelope`, `deep_collapse`, `contact_collapse`, and
`dangerous_motion`. Timers report the current continuous entering, outside, and
collapse candidates in seconds; candidate timers freeze once FALLEN is latched.
They are not recovery probabilities. The 205-input encoder ignores these fields.

The neutral replay in `docs/traces/step125-neutral-classification/` contains:

| Step |  Time | State      | Meaning                                      |
|------|------:|------------|----------------------------------------------|
| 0    | 0.000 | UNBALANCED | Airborne reset                               |
| 64   | 0.533 | BALANCED   | Standing envelope persisted through settling |
| 210  | 1.750 | UNBALANCED | Retention envelope violation persisted       |
| 462  | 3.850 | UNBALANCED | Legacy episode terminates unchanged          |
| 514  | 4.283 | UNBALANCED | First torso/head geometric contact           |
| 516  | 4.300 | FALLEN     | Deep collapse has persisted since step 493   |
| 642  | 5.350 | FALLEN     | Still latched at diagnostic end              |

There are no extra state oscillations in this replay. The FALLEN transition is
54 ticks later than legacy termination and two after first torso contact; the
contact itself is not what completes the fall timer. Production training still
stops at 462, because classifier state is observation-only.

Five C++ gates passed: classifier synthetic/integration tests, raw diagnostics,
body geometry, floor impulse audit, and the ten-second runtime baseline. The
classifier tests cover signed forward/back/lateral tilts, rapid rotation,
one/no-foot support, transient polygon collapse, negative margin, recovery,
entry/exit persistence, hysteresis, deep/contact collapse, artifact rejection,
latching/reset, immutable repeated observations, configurable timers, and exact
physical equality under different classifier configurations. Fourteen Python
diagnostic/classifier tests passed, including deterministic 642-step replay and
the unchanged archived Step 123/124 measurements. Application and Python-module
builds, Python compilation, and diff whitespace checks passed.

`verification.json` records exact agreement for all 643 pre-classifier raw
snapshots, 463 encoded input snapshots, and all 462 Python rewards. Of 136
checkpoint files, 135 retained their original hashes. `models/aura_balance.pt`
changed during the task (its modification time was later than the initial hash
inventory). The diagnostics/tests never load or save checkpoints or launch
training, and that file was left untouched. A file hash alone cannot distinguish
parameter changes from checkpoint metadata changes; all-model immutability
cannot be claimed for this interval.

Limitations: geometric foot proximity is not load-bearing support proof; the
existing pose/velocity correction inconsistency remains; low-height envelopes
can label crawling or deeply crouched configurations as failed standing. This
initial calibration has one real neutral trajectory plus synthetic measurement
fixtures, not validated disturbed-posture recovery trajectories. These rules
must be reviewed and calibrated further before replacing termination or reward.

## Step 126: classifier validation and checkpoint attribution

No production classifier, physics, reward, termination, action, observation
encoder, or learning code changed in this step. The explicit
`aura_balance_validation` target (`tests/tools/balance_validation.cpp`) runs isolated
headless scenarios without loading a policy or accessing checkpoints:

```sh
cmake --build build-release --target aura_balance_validation
./build-release/aura_balance_validation /path/to/new-validation-directory
```

The runner refuses an existing output directory. All physical cases run through
step 642, regardless of legacy termination, using the current zero targets and
motor settings. Tilted cases rotate all 16 parts about world `(0,0.6,0.25)`;
position and orientation change together, with zero initial velocities. The
horizontal case receives one common floor-clearance translation. Initial joint
gaps and relative rotations are checked against the untransformed body, and
initial floor penetration is rejected. No individual limb is repositioned.
Disturbance cases first run the neutral setup through step 80; a common velocity
impulse or rigid angular impulse is then added. Angular impulses add both omega
and `omega cross (position-COM)` to preserve rigid whole-body kinematics.

Artifacts are in `docs/traces/step126-validation/`. Each case has a CSV with
classification conditions, candidate timers, COM motion, support and nonfoot
contacts. `summary.json` contains timelines, durations, mechanical extrema and
deterministic physical-state digests; `verification.json` contains independent
trace checks and source/artifact hashes. State durations integrate intervals
between transition timestamps, including initial UNBALANCED time. The earlier
post-tick occupancy summary is retained as `summary-initial-posttick.json`; all
CSV measurements are byte-identical across the reporting correction.

| Physical scenario                    | BALANCED intervals (steps) | First FALLEN (step / seconds) | Balanced / unbalanced seconds |
|--------------------------------------|----------------------------|-------------------------------|-------------------------------|
| Neutral drop                         | 64–209                     | 516 / 4.300                   | 1.217 / 3.083                 |
| Forward 8 degrees                    | None                       | 274 / 2.283                   | 0 / 2.283                     |
| Forward 45 degrees                   | None                       | 141 / 1.175                   | 0 / 1.175                     |
| Backward 8 degrees                   | None                       | 282 / 2.350                   | 0 / 2.350                     |
| Backward 45 degrees                  | None                       | 138 / 1.150                   | 0 / 1.150                     |
| Left lateral 20 degrees              | None                       | 287 / 2.392                   | 0 / 2.392                     |
| Right lateral 20 degrees             | None                       | 235 / 1.958                   | 0 / 1.958                     |
| One-foot landing, 2-degree roll      | None                       | 382 / 3.183                   | 0 / 3.183                     |
| Upward impulse, +2 units/s           | 64–85; 146–237             | 637 / 5.308                   | 0.950 / 4.358                 |
| Edge landing, 5-degree diagonal tilt | None                       | 596 / 4.967                   | 0 / 4.967                     |
| Whole-body roll impulse, +3 rad/s    | 64–85                      | 417 / 3.475                   | 0.183 / 3.292                 |
| Small pitch impulse, +0.25 rad/s     | 64–163                     | 368 / 3.067                   | 0.833 / 2.233                 |
| Horizontal body near floor           | None                       | 24 / 0.200                    | 0 / 0.200                     |

All start UNBALANCED. Neutral follows U0/B64/U210/F516. The upward impulse case
follows U0/B64/U86/B146/U238/F637; feet are airborne from steps 81 through 124.
At re-entry 146, pelvis/torso tilt is about 0.37/0.46 degrees, COM vertical
velocity -0.109, both feet contact, and support margin about 0.310. Entry evidence
has persisted for 18 ticks, followed by 92 ticks of retained BALANCED state.
This is passive resettling with existing motors, not learned or independently
validated active recovery. The small pitch impulse does not produce a re-entry.

Seven separately labeled synthetic measurement fixtures test one-foot support,
four-tick loss of both feet, four-tick polygon degeneration, rapid rotation,
posture recovery, twelve-tick deep collapse followed by recovery, and sustained
deep collapse. One-foot support stays BALANCED. Four-tick disturbances remain
inside the exit grace period. Rotation/recovery/brief-collapse sequences follow
U0/B18/U30/B54; sustained collapse follows U0/B18/U30/F48 and remains latched.
These fixtures test exact state-machine behavior and do not represent physical
controller trajectories. Directional unit tests also include both lateral signs.

No physical case labels tilt of at least 45 degrees BALANCED. Every eventual
collapse is classified FALLEN within the recording window, after at least one
torso/head geometric proximity event and 24 consecutive collapse-evidence ticks.
No case re-enters the entry envelope after FALLEN. There are no unexplained state
oscillations; the extra U/B transitions in the airborne case follow disturbance
and resettling. Intentional exit hysteresis retains BALANCED for the first five
airborne/rapid-motion samples. Thus BALANCED is a filtered label, not an assertion
that every instantaneous measurement is inside its envelope.

The mild-angle cases are not examples of stable standing rejected solely for
tilt: they have negative support margins and/or significant velocity. The
2-degree one-foot landing has at most 13 consecutive entry-eligible ticks, below
the required 18. Strict settling thresholds may exclude other viable dynamic
recovery, which has not been ground-truthed. All physical states remain finite,
maximum joint gap stays below 1.91e-6 and measured floor penetration is zero.
These are integrity checks, not proof of physically accurate balance or energy.

The left/right physical cases differ by 52 ticks in FALLEN time. Classifier
posture limits are direction-neutral, and matched synthetic directional inputs
pass, but integrated trajectories are not symmetric. Diagnose this in the
existing solver/contact/motor dynamics before claiming symmetric recovery;
do not conceal it with direction-specific classifier thresholds. Upward pose
corrections with downward stored COM velocities remain present and must not be
counted as recovery.

Checkpoint investigation found a pre-existing interactive resume worker:
PID 46832, started 2026-10-14 at 15:02:43 local time, child of the existing CLI,
running `python -m cli.backend.nn_session --mode resume --checkpoint .../aura_balance.pt`.
It owns `../models/.aura_balance.pt.lock`. The checkpoint reports 664 episodes,
83 updates, seed 42 and run ID `475fd917102248de833e49af0528bbf3`.
`TrainingSession.train()` calls `TrainingRun.save_current()` at resume startup
and `TrainingRun.save()` after batches; these reach atomic
`save_checkpoint()`/`torch.save()`/`os.replace()`. The earlier process search
missed this module-style launch. This is the identified existing checkpoint
writer and a concrete explanation for normal saves during Step 125. No historical
filesystem audit exists to prove the PID of each past replacement retrospectively.
The worker was not interrupted or messaged, and no checkpoint was restored,
overwritten, copied or deleted. All 136 checkpoint hashes remained unchanged
through Step 126 validation: the diagnostic-write issue was not reproduced.
See `checkpoint-investigation.json` for preserved process and lock evidence.

Five relevant C++ gates and fourteen Python diagnostic/classifier tests passed.
All thirteen physical scenarios repeated deterministically, with complete
physical-state digests and identical CSVs on rerun. Seven synthetic fixtures
passed their expected transitions. Production source hashes match Step 125;
compilation and diff whitespace checks pass.

Before integration: investigate pose/velocity correction artifacts and lateral
asymmetry; evaluate additional controller-driven recovery trajectories; establish
ground truth for viable crouched/low postures and sensitivity to the strict
standing envelope. No demonstrated classifier correctness defect justified
changing thresholds here. Termination/reward integration remains deferred.

## Step 127: experimental versioned termination

`TerminationMode` and the pure `evaluateTermination()` helper live in
`include/aura/sim/Termination.hpp`. `AuraSimulation::terminationStatus()` is the
single source for terminated/truncated/reason, and `isDone()` uses its OR result.
The existing pelvis-based `isStanding()` remains unchanged as a legacy diagnostic.
The immutable selected mode survives reset; classifier state, timers and step
count still reset normally.

| Mode                     | Terminated condition                     | Default/use                              |
|--------------------------|------------------------------------------|------------------------------------------|
| `legacy` / version 1     | Existing pelvis standing predicate fails | All normal environments/training         |
| `balance_v2` / version 2 | Existing classifier is latched FALLEN    | Explicit raw diagnostic simulations only |

C++ selection is `AuraSimulation({}, TerminationMode::BalanceV2)`; omitting the
arguments retains legacy. Python raw selection is:

```python
simulation = aura_python.AuraSimulation(termination_mode="balance_v2")
```

`termination_mode` is also a read-only Python property. Invalid mode names fail
at construction. Raw observations add `termination_mode`, `termination_version`
and `reward_available`. Existing `is_standing` continues reporting the legacy
predicate in both modes; use `balance_state` for the physical classification.
Under V2 it is therefore valid for `is_standing=False`, `balance_state=UNBALANCED`,
and `done=False` to coexist.

Both modes independently truncate at 1,200 ticks. `done = terminated or truncated`.
Reasons are `none`, `fallen`, or `time_limit`. If physical/predicate termination
and the cap coincide, both flags are true and `fallen` takes reason precedence.
This preserves legacy flag/reason semantics while making the V2 mapping explicit.
Queries never advance the classifier. Reset clears latched FALLEN and truncation.

V2 deliberately has no reward yet: its raw `reward` is `None` and
`reward_available=False`. The training `AuraEnv` accepts only `legacy`; passing
`balance_v2` raises a diagnostic-only error before creating an environment.
`balance_reward()` also rejects any explicitly nonlegacy observation, including
attempted replacement of an existing environment's simulation with a V2 instance.
Its legacy arithmetic is unchanged, and observations without a mode field retain
backward-compatible legacy interpretation. No production training argument or
CLI choice enables V2. No trainer/optimizer code changed.

The standalone raw comparison tool does not assign a V2 reward or load models:

```sh
.venv/bin/python python/diagnostics/termination_diagnostics.py --output /path/to/new-comparison
```

`docs/traces/step127-termination/neutral.csv` records both modes' complete flags
through tick 642. Its summary and verification retain source/reference hashes
and legacy compatibility results. The existing thirteen-case C++ runner now
advances matched legacy and V2 instances, checks complete physical-state digests
at every tick, and records both first termination steps. Those artifacts are in
`docs/traces/step127-scenarios/`.

| Scenario                 | Legacy termination | V2 termination |
|--------------------------|-------------------:|---------------:|
| Neutral                  |                462 |            516 |
| Forward 8 degrees        |                207 |            274 |
| Forward 45 degrees       |                 75 |            141 |
| Backward 8 degrees       |                198 |            282 |
| Backward 45 degrees      |                 58 |            138 |
| Left lateral 20 degrees  |                181 |            287 |
| Right lateral 20 degrees |                148 |            235 |
| One-foot landing         |                303 |            382 |
| Brief airborne impulse   |                583 |            637 |
| Edge landing             |                219 |            596 |
| Rapid rotation           |                130 |            417 |
| Small pitch impulse      |                305 |            368 |
| Horizontal collapse      |                  0 |             24 |

V2 continues through BALANCED and UNBALANCED states, including airborne/support
loss and the physical re-entry to BALANCED at step 146. It ends exactly at the
existing FALLEN transition in every case. All thirteen physical traces are
byte-identical to Step 126; both modes and repeat runs have matching complete
physical-state digests. Longer survival is a changed endpoint, not evidence of
better control. In particular, edge landing gains 377 ticks without attaining
BALANCED; this illustrates why the legacy survival reward must not be used in V2.

Legacy compatibility checks matched all original raw fields in 643 snapshots,
the recorded pre-change 205 inputs through the original episode, and all 462
legacy rewards exactly (sum 451.60978722572327, reward-sequence SHA-256
`386eb68a476c03ca66520e6d2c65355832a2fcc073890a5599358a446a750a59`).
Python tests also compare encoded inputs between modes at all 643 snapshots.
The observation/action schema remains 2/5, with 205 inputs and 32 policy outputs.
All 136 checkpoint hashes remained unchanged during this task; no learning was
started and the existing interactive worker was left untouched.

Six C++ gates passed, including the new termination truth-table/integration
test, raw diagnostics, classifier fixtures, floor geometry/audit and runtime
baseline. Nineteen Python tests passed: five new mode/reward-guard tests and
fourteen existing classifier/diagnostic tests. Tests cover both modes, default
selection, reset/latching, repeated observations, recovery opportunity, V2 reward
unavailability, unsupported configuration, same-step fall/cap precedence, pure
time-limit truncation with nonfallen synthetic states, and actual truncation at
step 1200 after a latched physical fall. Application and module builds, Python
compilation and diff whitespace checks passed.

Files added/changed for this step: `Termination.hpp`, `AuraSimulation.hpp/.cpp`,
`AuraPython.cpp`, `python/aura_env.py` (guards/configuration only),
`python/diagnostics/termination_diagnostics.py`, `tests/simulation/termination_tests.cpp`,
`tests/simulation/test_termination_modes.py`, `tests/tools/balance_validation.cpp`, CMake test
registration and this document. The classifier configuration/implementation,
physics stepping and policy implementation retain their Step 126 hashes.

Before activation, implement and validate a V2 reward, then record environment
and reward versions in any new learning artifacts. Existing checkpoint return
values and survival results describe legacy episodes; they are not directly
comparable to V2. Pose/velocity correction artifacts, strict standing-envelope
limitations and lateral dynamics asymmetry remain. Numerical simulation errors
remain separate from physical fall classification. Production defaults must stay
legacy until the reward and evaluation review is complete.

## Step 128 — Diagnostic Balance Reward V2 (2026-10-14)

Reward V2 is implemented and inspectable, but **production training remains
legacy**. Neither network was trained by this task. `BalanceV2Diagnostics` is an
explicit raw-info wrapper returning a dictionary, not the trainer's environment
interface. Raw C++ `balance_v2` still reports no reward; Python owns this objective.
`AuraEnv(termination_mode="balance_v2")` remains blocked pending reward and
balance-quality metric review. The physical classifier and termination rules are
unchanged. Diagnostic measurements do not enter the 205-input encoder.

### Formula, physical scales and weights

Let `s(x) = clamp(x,0,1)^2 * (3 - 2*clamp(x,0,1))`. All qualities below are
unitless and bounded in [0,1]. Heights and linear speeds use the simulator's world
length units; angular speeds use rad/s. References come from the existing neutral
body geometry **before the 0.6-unit reset lift**, not the airborne reset height:
pelvis 4.17, torso 6.0, mass-weighted COM approximately 3.75625.

- `P = exp(-0.5*((acos(up_p)/30deg)^2 + (acos(up_t)/30deg)^2))`.
  Pelvis and torso both matter; head and individual limb angles are not scored.
  Thirty degrees is a smooth standing-objective scale, not a termination limit.
  Equal 45-degree pelvis/torso tilts give P≈0.105 rather than broad standing credit.
- `H = sqrt(s((h_p/h_p0-.25)/.50) * s((h_t/h_t0-.25)/.50))`.
  Height credit reaches its plateau at 75% of neutral. Thus an upright controlled
  crouch at 80% is free, and taller-than-neutral poses add no credit. Below 25%
  is the zero-quality collapse region. The geometric mean demands both heights,
  without adding two highly correlated height rewards.
- `v0=.25*sqrt(g*h_COM0)` (~1.518 units/s),
  `w0=.5*sqrt(g/h_COM0)` (~0.808 rad/s), `g=9.81`.
  Set `zv=max(0,abs(COMvy)-.25)/v0`,
  `zh=max(0,hypot(COMvx,COMvz)-.35)/v0`,
  `zp=max(0,|omega_p|-.4)/w0`, and the analogous `zt`.
  `M=exp(-(zv²+zh²+.5*(zp²+zt²)))`. The deadbands permit small controlled
  shifts; gravitational length/time scales normalize larger motion. This is a
  standing objective, not a locomotion objective. COM velocity and angular
  velocity detect dangerous motion that static uprightness misses.
- Support target `S*=0` without either foot; `.25` with geometric foot contact
  but fewer than three hull vertices; otherwise
  `S*=.25+.75*s((margin/.10+1)/2)`.
  The .10-unit transition spans roughly 15% of the .65-unit foot width.
  One supported foot is sufficient. Negative margin reduces quality continuously
  without declaring an unrecoverable fall. This measures **geometric** support,
  never a claimed ground reaction force.
  `S_t=S_(t-1)+(1-exp(-dt/.08))*(S*-S_(t-1))`, `dt=1/120`.
  Reset initializes S to measured support. The 80 ms filter spans about ten
  ticks, preventing one contact-tolerance sample from dominating reward.
- `Phi=P*M*S` (height deliberately excluded).
  `D=.995*Phi_t-Phi_(t-1)`. At either termination or truncation, end Phi=0.
  A pure upward pose correction cannot earn recovery credit. Improvement and
  regression both contribute; state-label switching adds nothing. For the same
  discount, `sum(gamma^(t-1)*D_t)=gamma^T*Phi_T-Phi_0`, so repeated cycles cannot
  farm this shaping term. The current REINFORCE discount is .995; any future
  consumer must match it or explicitly redesign this term.

The complete per-tick objective is:

```text
R = .25*(P-1) + .15*(H-1) + .25*(M-1)
    + I(not terminated)*P*H*M*S
    + .10*(.995*Phi_t-Phi_(t-1))
    - I(terminated)
```

There is no constant survival payment. The positive support component requires
posture, height, motion and support together; separate modest penalties prevent
uncontrolled collapse from earning positive credit when that product approaches
zero. Correlated qualities are gates rather than four independent positive
bonuses. Weights are initial calibration choices, not optimized or validated
learning hyperparameters. One terminal penalty equals roughly one ideal standing
tick, keeping it smaller than the accumulated physical feedback.

| Component |  Weighted range | Purpose                                     |
|-----------|----------------:|---------------------------------------------|
| posture   |      [-0.25, 0] | Pelvis and torso uprightness                |
| height    |      [-0.15, 0] | Collapse avoidance with a crouching plateau |
| motion    |      [-0.25, 0] | Excess COM and rotational motion            |
| support   |          [0, 1] | Controlled physically supported posture     |
| recovery  | [-0.10, 0.0995] | Bounded physical progress, no label bonus   |
| terminal  |         {-1, 0} | Existing established FALLEN signal only     |

Conservative total bound: **[-1.75, 1.0995]**. Ideal stationary supported
standing receives 0.9995 per tick. Inputs must be finite; invalid observations
raise an error rather than silently producing a reward. Very large finite speed
is safely mapped toward zero motion quality. Returned `reward_components` are
already weighted and their sum is exactly `reward_total`; `reward_terms`,
`reward_qualities`, and `reward_weights` explain the calculation. The raw info
retains balance state and termination reason. Reset earns no transition reward;
updates require consecutive physics ticks, and reward stops at episode end.

### Measurement tests and actual physical replay

Fifteen reward/contract tests cover supported standing, mild tilt, symmetric severe
pitch/roll, rapid rotation, controlled COM motion, crouching, airborne movement,
one-foot support, polygon degeneration, recovery, continued collapse, terminal
fall, truncation, label oscillation, finite/invalid values, exact component sums,
potential telescoping, tick/version guards, checkpoint metadata and the live
wrapper. These fixtures prescribe measurements; they do not establish actual
physical recoverability. Head/arm movement has no direct cost when pelvis, torso,
COM and support measurements are held fixed. Actual arm gestures and deep crouch
trajectories still need physical evaluation; no gesture policy was introduced.

Old diagnostic traces lacked angular velocities and complete support geometry.
The same 13 native scenarios were rerun with those additional measurements. **Every previously recorded field is
identical to Step 127**, and paired legacy/
V2 physical hashes match at every tick. Each full physical scenario was repeated;
reward replay was also repeated exactly. There are 8,359 physical snapshots per
pass, including post-termination diagnostics. Reward accumulation ends on the
first V2 terminal tick. Crouching and ideal recovery rankings are fixture tests;
the native cases are passive zero-target physics, not learned control.

The following totals are undiscounted diagnostic sums, not comparable historical
learning scores. State averages use the post-tick label (reset omitted, terminal
tick included); seconds are rewarded ticks / 120.

| Scenario            | V2 total | B average | U average | Terminal | B seconds | U seconds | Legacy episode total |
|---------------------|---------:|----------:|----------:|---------:|----------:|----------:|---------------------:|
| brief_airborne      |  -38.474 |    0.7629 |   -0.2372 |  -1.6500 |     0.950 |     4.350 |              574.147 |
| edge_landing        | -249.897 |         — |   -0.4172 |  -1.6500 |     0.000 |     4.958 |              211.832 |
| horizontal_collapse |  -12.626 |         — |   -0.4811 |  -1.5599 |     0.000 |     0.192 |                0.000 |
| left_lateral        | -130.276 |         — |   -0.4497 |  -1.6500 |     0.000 |     2.383 |              169.573 |
| mild_backward       |  -87.072 |         — |   -0.3040 |  -1.6500 |     0.000 |     2.342 |              190.282 |
| mild_forward        |  -72.491 |         — |   -0.2595 |  -1.6500 |     0.000 |     2.275 |              195.504 |
| neutral             |   26.247 |    0.9097 |   -0.2843 |  -1.6500 |     1.217 |     3.075 |              451.610 |
| one_foot_landing    |  -76.054 |         — |   -0.1953 |  -1.6500 |     0.000 |     3.175 |              296.967 |
| rapid_rotation      | -180.678 |    0.6462 |   -0.4905 |  -1.6499 |     0.183 |     3.283 |              124.281 |
| recovery_attempt    |   26.594 |    0.9051 |   -0.2332 |  -1.6494 |     0.833 |     2.225 |              292.573 |
| right_lateral       | -102.062 |         — |   -0.4291 |  -1.6500 |     0.000 |     1.950 |              140.755 |
| severe_backward     |  -72.623 |         — |   -0.5182 |  -1.6235 |     0.000 |     1.142 |               47.896 |
| severe_forward      |  -72.250 |         — |   -0.5043 |  -1.6496 |     0.000 |     1.167 |               61.417 |

All 13 cases eventually fall; a longer terminal time is not evidence of better
balance. For example the horizontal-collapse case has fewer negative ticks and a
less negative total than several long falling cases. This is an objective risk,
not a success claim.

| Scenario            | Posture sum | Height sum | Motion sum | Support sum | Recovery sum | Terminal sum |
|---------------------|------------:|-----------:|-----------:|------------:|-------------:|-------------:|
| brief_airborne      |    -58.9296 |    -7.3989 |  -101.1398 |    130.0591 |      -0.0650 |      -1.0000 |
| edge_landing        |    -98.6226 |   -43.9012 |  -133.7259 |     27.3659 |      -0.0137 |      -1.0000 |
| horizontal_collapse |     -5.9993 |    -3.6000 |    -2.0264 |      0.0000 |      -0.0000 |      -1.0000 |
| left_lateral        |    -48.6963 |   -14.2644 |   -66.5519 |      0.2372 |      -0.0001 |      -1.0000 |
| mild_backward       |    -29.2288 |   -10.3084 |   -54.8185 |      8.2878 |      -0.0041 |      -1.0000 |
| mild_forward        |    -31.3424 |    -8.5829 |   -49.9883 |     18.4322 |      -0.0092 |      -1.0000 |
| neutral             |    -49.0812 |    -7.4866 |   -75.5624 |    159.4568 |      -0.0797 |      -1.0000 |
| one_foot_landing    |    -30.8772 |   -10.0753 |   -73.6895 |     39.6074 |      -0.0198 |      -1.0000 |
| rapid_rotation      |    -79.4674 |   -39.6758 |   -89.1258 |     28.6055 |      -0.0143 |      -1.0000 |
| recovery_attempt    |    -40.0927 |    -6.8249 |   -47.2716 |    121.8446 |      -0.0609 |      -1.0000 |
| right_lateral       |    -36.1772 |   -11.2724 |   -53.7480 |      0.1355 |      -0.0001 |      -1.0000 |
| severe_backward     |    -32.5688 |   -10.7725 |   -28.3207 |      0.0386 |      -0.0000 |      -1.0000 |
| severe_forward      |    -33.3447 |    -8.6672 |   -29.3378 |      0.1000 |      -0.0001 |      -1.0000 |

### Neutral timeline

| Phase / step                      | Classification |       Reward |
|-----------------------------------|----------------|-------------:|
| Initial airborne drop, steps 1–42 | U              | mean -0.1335 |
| Settled controlled period, 64–180 | B              | mean +0.9740 |
| 200, early degradation            | B              |      +0.6213 |
| 210                               | U              |      +0.4306 |
| 238                               | U              |      +0.0266 |
| Collapse, 300–515                 | U              | mean -0.4887 |
| 462, legacy endpoint              | U              |      -0.5506 |
| 516, established fall             | F              |      -1.6500 |

Balanced mean +0.9097 versus unbalanced mean -0.2843. Legacy still ends at
462 and earns **451.60978722572327**, with its exact historical reward sequence
SHA256 unchanged. V2 ends at 516 and earns 26.2469685 on its different objective.
Applying the old formula along all 516 V2 ticks would yield 486.8232312, but that
is a counterfactual formula comparison, **not a valid legacy episode score**.
The extra collapse duration now contributes negative reward rather than continued
survival credit. Neither score change nor the extra ticks shows improved control.

### Airborne recovery timeline

| Step / phase                       | Classification |       Reward | Weighted recovery |
|------------------------------------|----------------|-------------:|------------------:|
| 80, before impulse                 | B              |      +0.9595 |         -0.000036 |
| 81, upward impulse                 | B              |      +0.0718 |         -0.066024 |
| 124, still airborne                | U              |      -0.1466 |         -0.000091 |
| 125, landing improvement           | U              |      +0.0600 |         +0.008642 |
| 135, support settling              | U              |      +0.6882 |         +0.003113 |
| 145, improving before label change | U              |      +0.8895 |         +0.000774 |
| 146, classifier reentry            | B              |      +0.9003 |         +0.000648 |
| Controlled period, 146–180         | B              | mean +0.9593 |                 — |
| 637, eventual fall                 | F              |      -1.6500 |   approximately 0 |

Reward improves **before** the classifier changes labels, because physical
measurements improve. This passive resettling after an imposed impulse is not a
learned recovery. Full episode total is -38.4743, reflecting its later collapse;
legacy total 574.1475 covers its different 583-tick episode.

### Compatibility, files, and checks

Changed for Step 128:

- `python/balance_reward_v2.py`: independent six-component evaluator/config.
- `python/aura_env.py`: legacy reward metadata and opt-in raw diagnostic wrapper;
  legacy `balance_reward()` source preserved byte for byte.
- `python/model_checkpoint.py`: new saves identify legacy termination/reward;
  contract validation rejects objective mismatches. Missing historical objective
  metadata defaults to legacy; no historical checkpoint migration.
- `include/aura/sim/AuraSimulation.hpp`, `src/sim/AuraSimulation.cpp`, and
  `src/python/AuraPython.cpp`: expose the three existing neutral height references
  in raw diagnostic info. No encoder or physical stepping changes.
- `tests/tools/balance_validation.cpp`: record missing reward measurements/end flags.
- `tests/historical/test_balance_reward_v2.py`: deterministic physical measurement and contract
  tests; fresh temporary checkpoint with **zero optimizer updates**.
- `python/diagnostics/reward_diagnostics.py`: deterministic replay/report command.
- This architecture record and `docs/traces/step128-{physical,rewards}/` artifacts.

Build passed for application, Python extension, validation runner and relevant
headless targets. Six C++ gates passed: termination_modes, balance_classifier,
balance_diagnostics, runtime_baseline, body_geometry and floor_collision_audit.
Python checks: 15 new reward tests plus the existing 19 classifier, diagnostics,
post-termination and termination tests (34 total). Compilation and whitespace
checks passed. Existing NumPy-unavailable and macOS OpenGL deprecation warnings
remain warnings; diagnostics and CPU tests ran successfully.

Checkpoint verification found **135 of 136 hashes unchanged**, including archived
experiments and best snapshots. The active `models/aura_balance.pt` changed while
a pre-existing CLI resume worker (PID 46832, started 15:02:43 local) was still
running; its observed save time was 16:18:40. This task did not start that worker,
stop it, restore its file, or train either network. The one new checkpoint in the
tests was untrained and confined to an automatically removed temporary directory.
See `step128-rewards/verification.json` for hashes and exact legacy/live checks.

Replay command (both destinations must be fresh):

```sh
./build-release/aura_balance_validation docs/traces/step128-physical
PYTHONPATH=build-release:. .venv/bin/python -m python.diagnostics.reward_diagnostics docs/traces/step128-physical docs/traces/step128-rewards
```

### Calibration risks before Step 130

1. **Early-failure incentive:** accumulated negative rewards during a long loss of
   control may favor falling sooner when recovery is difficult. Before training,
   compare matched trajectories with plausible recovery opportunities using the
   actual discounted objective. Do not compensate with an arbitrary huge penalty.
2. **Geometric support:** tolerance-based contacts do not establish load bearing.
   Hand/wall-supported upright poses can receive credit if feet also geometrically
   touch. Ground reaction/load attribution remains missing. COM outside support
   is intentionally not a terminal signal.
3. **Filter memory:** 80 ms support smoothing can retain short unsupported credit
   and adds history that is not represented explicitly in the unchanged 205 inputs.
   Review whether that hidden objective state is acceptable for a future critic.
4. **Pose corrections:** height can still reflect solver correction. The plateau
   limits height farming and the potential excludes height, but physical posture/
   support can also change through correction. This reward cannot certify active
   control from solver motion alone.
5. **Standing versus active movement:** moderate motion and crouching are allowed,
   but rapid successful stepping/recovery can receive an immediate motion cost.
   Validate real movements, contact transients and deeper crouches before setting
   weights. Independent arm/head motion has no direct penalty, but its true effect
   on COM/torso is appropriately observable.
6. **Shaping contract:** the .995 discount and zero end potential must match future
   training/bootstrapping conventions. Truncation is not a fall penalty. Initial
   potential contributes a posture-dependent constant across different resets;
   compare unshaped components as well when comparing starting conditions.
7. **Scale and tradeoffs:** reward per tick is tied to fixed 120 Hz. Coefficients,
   30-degree posture scale, deadbands, .10 support margin and 80 ms filtering need
   validation together with independent balance-quality metrics, not score alone.
   Full-episode recovery shaping sums can be negative even after a temporary
   recovery; it provides local information and is not a count of recoveries.

Next is Step 129: review these reward timelines together with balanced duration,
controlled posture/motion, contact quality, recovery quality and mechanical
validity. Only explicit subsequent approval should enable a versioned V2 training
environment. Existing critics predict the legacy objective and require new V2
training/validation before influencing actor updates under this reward.

## Step 129 — Balance Reward V2 incentive audit (2026-10-14)

**Finding: the unchanged objective has an early-fall incentive in negative-quality
constructed trajectories. The fixed -1 terminal component is insufficient to
remove it.** Prompt constructed recovery is strongly profitable, but 12 of the
63 deliberately constructed recovery comparisons lose to quick failure after
discounting. These counts describe a chosen grid, not an empirical probability.
No experiment establishes that a learned actor can execute the exploit, or that
these synthetic recoveries are dynamically feasible. V2 training stays blocked.

### Method and controls

The audit calls the actual `BalanceRewardV2` and the production
`python.train_nn.discounted_returns` with **gamma=0.995**, verifying its result
against a direct discounted sum. It changes no reward coefficient or definition.
`aura_incentive_fixture_classifier` runs the existing C++ `BalanceClassifier` and
`evaluateTermination` on prescribed measurements. No classifier copy, manual
fall labels, changed thresholds or alternate physics are used. Collapse must
persist for the existing 24 ticks. Fall at the cap can set both end flags.

There are **106 cases**: 13 previously realized physical traces and 93 constructed
measurement sequences. The grid contains three instability qualities, seven
waiting durations (0, 24, 120, 240, 480, 720, 900 ticks), and three recovery ramps (24, 60, 120 ticks), plus
rapid/delayed collapse, unstable truncation, ideal
standing/recovery references, and cap-boundary checks.

All rewards are calculated from measurements. Synthetic poses use the actual
neutral height references, normalized upright vectors, finite rates and a support
polygon whose margin agrees with the prescribed COM projection. Ramp angular
speeds and COM speeds are sampled pose derivatives. **This does not establish
joint-anchor consistency, contact feasibility, force balance, bounded actuation,
or feasible accelerations.** In particular, a stationary tilted COM outside the
foot hull would need additional support or active dynamics; holding that fixture
for seconds is a mathematical stress condition, not a simulated recovery attempt.
Abrupt ideal-recovery and cap fixtures are explicitly artificial. No rewards are
hand-assigned to states or trajectories.

Constructed branches share standing setup, 24 ticks transitioning into instability
and six settling ticks. Their fork is **step 48**. At each instability quality,
starting observations, filtered support, previous recovery potential and prior
history match exactly. Comparisons include only rewards from step 49 onward;
no branch resets reward memory at the decision point. The 1200-step limit remains
absolute, leaving 1152 ticks (9.6 s) from this fork.

- Mild fixture: pelvis/torso 25 degrees, support margin +.02, neutral heights.
- Severe fixture: 35 degrees, support margin -.10, neutral heights.
- Very severe fixture: 55 degrees, support margin -.10, neutral heights.

Stable fixture: upright, stationary, geometric support margin +.20, neutral
heights. Collapse interpolates over 24 ticks toward 90-degree tilt, 20% height
references and absent foot support, then holds long enough for the real classifier
to establish FALLEN. Recovery interpolates back to stable measurements and assumes
ideal standing afterward. That last assumption makes delayed recovery comparisons
optimistic. All comparison horizons include actual terminal/truncated handling.

### Main same-start comparison

The following rows share the severe fixture's step-48 context. Larger return is
preferred. Times are post-fork post-tick classifications; one terminal tick is
reported separately and is not counted as UNBALANCED. The penalty column contains
only the weighted terminal component, not all physical costs on the terminal tick.

| Continuation from same state                  | Undiscounted | Discounted | B seconds | U seconds | Discounted fall penalty |
|-----------------------------------------------|-------------:|-----------:|----------:|----------:|------------------------:|
| Artificial immediate recovery upper reference |     1145.467 |    193.646 |     9.450 |     0.150 |                  0.0000 |
| Recover over 0.5 s, no wait                   |     1087.654 |    143.405 |     8.958 |     0.642 |                  0.0000 |
| Wait 2 s, recover over 0.5 s                  |      818.722 |     25.907 |     6.958 |     2.642 |                  0.0000 |
| Wait 6 s, recover over 0.5 s                  |      280.477 |    -20.044 |     2.958 |     6.642 |                  0.0000 |
| Wait 7.5 s, recover over 0.5 s                |       78.635 |    -22.751 |     1.458 |     8.142 |                  0.0000 |
| Quick collapse, FALLEN after 46 ticks         |      -22.979 |    -20.599 |     0.000 |     0.375 |                 -0.7981 |
| Wait 2 s then same collapse                   |      -51.808 |    -22.844 |     0.000 |     2.375 |                 -0.2397 |
| Unbalanced hold until truncation              |     -139.961 |    -23.901 |     0.000 |     9.600 |                  0.0000 |

Thus quick failure (-20.599) outranks both delayed failure (-22.844) and one late
successful recovery (-22.751), even though that recovery earns positive total
undiscounted reward (+78.635). The recovery has 175 BALANCED ticks before the cap;
those late positive rewards receive little weight. In contrast, prompt recovery (+143.405) and recovery after a
two-second wait (+25.907) easily beat quick failure.
This is not evidence that attempting recovery is generally worse: the incentive
depends on delay, severity, recovery quality and probability of eventual success.

As a separate unmatched reference, ideal standing from reset to step 1200 earns
1199.3005 undiscounted and 199.4117 discounted, with 9.858 s BALANCED and .142 s
initial classifier entry. It starts from upright support and is **not** included
as a same-start competitor to the unstable branches.

### Recovery grid: every recovery scored from its common unstable start

Asterisk marks a return below that quality's quick-fall alternative. The delayed
fall column uses the same wait and collapse path, not a different terminal rule.
Each row's recovery then assumes ideal standing until truncation.

**Mild**; quick-fall discounted return -20.3649.

| Wait ticks / seconds | 24-tick recovery | 60-tick recovery | 120-tick recovery | Delayed fall |
|----------------------|-----------------:|-----------------:|------------------:|-------------:|
| 0 / 0.00             |         169.9161 |         168.8343 |          171.2156 |     -20.3649 |
| 24 / 0.20            |         156.3984 |         155.2050 |          157.2371 |     -12.1937 |
| 120 / 1.00           |         115.0909 |         114.3403 |          115.5919 |      11.1345 |
| 240 / 2.00           |          84.8609 |          84.4496 |           85.1354 |      28.1750 |
| 480 / 4.00           |          59.2175 |          59.0940 |           59.2999 |      42.6300 |
| 720 / 6.00           |          51.5171 |          51.4800 |           51.5418 |      46.9707 |
| 900 / 7.50           |          49.5529 |          49.5379 |           49.5630 |      48.0779 |

**Severe**; quick-fall discounted return -20.5989.

| Wait ticks / seconds | 24-tick recovery | 60-tick recovery | 120-tick recovery | Delayed fall |
|----------------------|-----------------:|-----------------:|------------------:|-------------:|
| 0 / 0.00             |         168.2835 |         143.4050 |          141.1398 |     -20.5989 |
| 24 / 0.20            |         146.6521 |         124.5142 |          122.3012 |     -20.6592 |
| 120 / 1.00           |          81.2539 |          67.5674 |           66.1885 |     -21.9086 |
| 240 / 2.00           |          33.4073 |          25.9073 |           25.1517 |     -22.8435 |
| 480 / 4.00           |          -7.1798 |          -9.4319 |           -9.6588 |     -23.6366 |
| 720 / 6.00           |         -19.3676 |         -20.0439 |          -20.1120 |     -23.8748 |
| 900 / 7.50           |       -22.4764 * |       -22.7507 * |        -22.7783 * |     -23.9355 |

**Very Severe**; quick-fall discounted return -20.8028.

| Wait ticks / seconds | 24-tick recovery | 60-tick recovery | 120-tick recovery | Delayed fall |
|----------------------|-----------------:|-----------------:|------------------:|-------------:|
| 0 / 0.00             |         167.1345 |         128.2225 |          107.9735 |     -20.8028 |
| 24 / 0.20            |         142.8039 |         108.3880 |           90.3900 |     -23.6694 |
| 120 / 1.00           |          70.2656 |          48.9999 |           37.8740 |     -32.3790 |
| 240 / 2.00           |          17.2150 |           5.5617 |           -0.5351 |     -38.7520 |
| 480 / 4.00           |       -27.7865 * |       -31.2858 * |        -33.1166 * |     -44.1581 |
| 720 / 6.00           |       -41.2999 * |       -42.3507 * |        -42.9005 * |     -45.7815 |
| 900 / 7.50           |       -44.7468 * |       -45.1731 * |        -45.3961 * |     -46.1956 |

Three late 35-degree cases and nine late 55-degree cases lose to quick failure.
At 55 degrees, waiting four seconds and then recovering over 0.5 s earns +478.335
undiscounted but -31.286 discounted, below quick failure's -20.803. These artificial
sequences expose utility tradeoffs; they do not establish which recoveries AURA
can actually perform. A physically verified active recovery comparison remains
missing.

### Physical evidence and its limits

All 13 native traces reproduce their Step 128 rewards exactly. The native helper
also reproduces their recorded state labels and end flags exactly. Four branches
share the complete physical/reward history through step 80, including filter
memory; external diagnostic impulses then differ. The table scores only the tail
from step 81. These are passive zero-target simulations, not policy interventions.

| Physical continuation       | Undiscounted | Discounted | B seconds | U seconds | Fall step | Discounted fall penalty |
|-----------------------------|-------------:|-----------:|----------:|----------:|----------:|------------------------:|
| Neutral continuation        |        3.326 |     66.004 |     1.075 |     2.550 |       516 |                 -0.1130 |
| Airborne passive resettling |      -61.396 |     34.449 |     0.808 |     3.825 |       637 |                 -0.0616 |
| Rapid rotation disturbance  |     -203.599 |    -92.319 |     0.042 |     2.758 |       417 |                 -0.1856 |
| Small pitch disturbance     |        3.673 |     40.546 |     0.692 |     1.700 |       368 |                 -0.2373 |

The fastest physical fall (small pitch, step 368) does **not** score best; neutral
continuation through step 516 does. The rapid-rotation branch is the worst return,
even though airborne resettling continues much longer. Therefore these actual
traces do not demonstrate a universal preference for shorter episodes. Their
returns reflect differing motion quality as well as length. The airborne recovery
is passive settling, not learned recovery; the `recovery_attempt` filename does
not establish successful recovery. All ultimately fall. No genuine learned
recovery is present in the available traces.

Earlier traces lack the complete angular/support measurements needed for Reward
V2. Step 128 reran the archived configurations and proved prior physical columns
unchanged; this audit uses those sufficient recorded measurements without a new
simulation or training run.

### Why the length incentive exists

Negative physical reward stops when the episode terminates. Discounting also
makes a distant penalty smaller, but the accumulated unstable costs can dominate.
For illustration, a constant living reward c and terminal reward f at tick N give
`G_N=c*(1-gamma^(N-1))/(1-gamma)+gamma^(N-1)*f`.
One additional negative-quality tick before the same terminal outcome changes
return by `gamma^(N-1)*(c-(1-gamma)*f)`. Delaying is preferred only if
`c >= (1-gamma)*f`. This identity is explanatory, not a fabricated reward trace;
all numerical cases above use the actual reward function and classifier.

Gamma .995 has a **138.28-tick half-life (1.152 s)** and an effective geometric
horizon of about **200 ticks (1.667 s)**. Late recovered standing after several
seconds can have much less weight than the earlier recovery cost. Both the
finite 1200-step cap and the discount matter when interpreting late successes.

For matched starts and either ending, recovery shaping telescopes:

```text
sum(gamma^(t-1) * .1*(gamma*Phi_t-Phi_(t-1)))
    = .1*(gamma^T*Phi_T-Phi_start)
    = -.1*Phi_start          (end potential is zero)
```

Its discounted contribution is identical across matched branches (verified to
12 decimal places). For the severe cohort it is -0.0109596263 regardless of
recovery, fall or truncation. Shaping offers local learning feedback but cannot
repair the episode-length preference of the underlying physical objective.
Increasing the shaping weight alone would not solve this comparison.

### Is the terminal penalty sufficient?

No, for the negative-quality fixtures above. Each FALLEN case gets its single
-1 terminal component; after discounting it is only -0.7981 for the quick fall,
-.2397 after a 240-tick wait, and -.00877 after a 900-tick wait. The full terminal
reward also includes posture/height/motion/shaping costs; those are included in
every reported total.

Analytical sensitivity isolates the current terminal component without changing
or rerunning a tuned reward: if only its weight K changed, a branch return would
be `G(K)=G(1)+(K-1)*discounted_terminal_component(1)`. The smallest weight that
would stop quick failure outranking every comparator in this particular grid is:

| Fixture family | Current weight | Grid-specific required weight |
|----------------|---------------:|------------------------------:|
| mild           |              1 |                        1.0000 |
| severe         |              1 |                        5.2273 |
| very severe    |              1 |                       33.1712 |

**These are sensitivity bounds, not proposed coefficients.** Their dependence on
severity shows why fitting a larger penalty to one trace/grid is inadequate. A
large penalty can dominate feedback and still lack a justified task utility.
There is no evidence here that a particular universal fixed penalty is optimal.

### Expected recovery versus guaranteed success

A success trajectory is not an expected recovery value. For an explicitly
constructed binary lottery, `E[G]=p*G_success+(1-p)*G_delayed_failure`.
The break-even success probability against quick failure is
`p*=(G_quick-G_delayed_failure)/(G_success-G_delayed_failure)`.
No actual recovery probability has been measured. For the 35-degree fixture with
a 60-tick recovery ramp:

| Wait seconds | Required success probability |
|--------------|-----------------------------:|
| 0.20         |                       0.042% |
| 1.00         |                       1.464% |
| 2.00         |                       4.604% |
| 4.00         |                      21.385% |
| 6.00         |                      85.512% |
| 7.50         |                     281.616% |

A threshold above 100% means even guaranteed success in that late constructed
trajectory loses to quick failure. At six seconds the threshold is already
85.5%; at two seconds it is 4.6%. Different failure paths would produce different
thresholds. This calculation illustrates uncertainty, not a learned probability
of recovery or an empirical claim about AURA's capability.

### Unbalanced survival, extremes, and cap handling

The mild unbalanced hold remains UNBALANCED through truncation and earns +48.683
discounted, versus -23.901 for the 35-degree unsupported-margin hold and -46.334
for the 55-degree hold. Reward is continuous physical quality, not the state
label. Mild imperfect support is profitable but is still dominated by controlled
standing from the same context. This alone is not a confirmed exploit. Whether
an attainable marginal posture can outscore active balance/recovery under its
risk requires actual controlled trajectories and independent balance metrics.

Finite float32-range stress fixtures (up to 1e30 motion coordinates) remain finite
and saturate motion quality at zero. Ideal supported standing earns .9995 on a
normal tick, a stationary deeply tilted/low unsupported pose approximately -.4,
and its extreme-motion counterpart approximately -.65. Numerical validity is
not evidence that those extreme states are physically reachable. The conservative
existing total bound remains [-1.75, 1.0995], so huge valid speeds do not produce
unbounded penalties or rewards. Negative-quality terminal ticks can approach
-1.65, but that bounded one-time cost does not automatically offset many negative
living ticks.

The cap fixtures share history through step 1176. One branch has 24 consecutive
collapse samples before step 1200 and is both terminated and truncated. The
other has 23 and is UNBALANCED/truncated only. The unchanged native classifier
supplies these outcomes. Final rewards are approximately -1.3999703 versus
-.3999704; the former has the -1 terminal component, the latter has none. Support
reward is zero in both (height quality zero); tiny shaping differences reflect
one extra support-filter tick. Both endings zero the potential. The production
return calculation stops on either end flag with no bootstrap. A truncation is
not treated as an established fall, and delaying collapse beyond the cap can
avoid the fall component. This is a finite-horizon utility issue to review, not
an implementation mismatch in flags.

### Recommendations for the next revision — not implemented

1. **Define the utility of failure over a common task horizon before tuning any
   weights.** Test a virtual absorbing-failure outcome through the remaining
   horizon, or its equivalent discounted terminal charge. Physical simulation may
   still stop at FALLEN; the objective must specify the value of the remaining
   task opportunity. This would remove the free disappearance of future costs.
   The failure utility needs an explicit justified scale and tests across
   recoverable/unrecoverable states; do not equate every temporary U state to it.
2. A useful conservative *candidate for analysis* is the existing nonterminal
   unshaped lower bound, `-(.25+.15+.25)=-.65` per virtual tick. It is derived from
   current component ranges, not optimized on the neutral trace. It is not an
   adopted reward: treating the entire remainder as worst-quality failure can
   create a large effective terminal cost and requires review. Time-dependent
   terminal charging also affects the critic's value target and the treatment of
   remaining episode time under the fixed 205 observations.
3. Review the .995 discount against demonstrated recovery times. Raising gamma
   alone does not fix negative living costs disappearing at termination, and any
   later change must also update the potential-shaping discount and invalidate
   old critic targets. Choose it from the task timescale, not this grid's score.
4. Compare a revised failure utility against both prompt/slow successful recovery
   and deliberately delayed collapse. Preserve the intended distinction: futile
   prolonged uncontrolled collapse may deserve a worse return, while physically
   attainable recovery should earn enough benefit for its risk and delay.
   Gathering feasible controlled recovery/failure branches is the missing next
   evidence before claiming a policy-level fix.
5. Avoid a blind large fixed penalty, label-transition bonuses, or adding a
   living constant without examining length effects. A living offset changes
   policy preferences in variable-length episodes and can reward merely stalling;
   treating it as a harmless numerical normalization would be incorrect.

No proposed adjustment was applied. Balance-quality metrics should be reviewed
with the corrected objective before V2 training or critic retraining is enabled.

### Files, tests, reproduction, and preservation

Only audit tooling/tests, its build target, documentation and audit artifacts were
added for this step:

- `python/diagnostics/reward_incentive_audit.py`: physical replay, prescribed sequences,
  actual discounted returns, branch comparisons, penalty sensitivity and lotteries.
- `tests/tools/incentive_fixture_classifier.cpp`: applies unchanged native classifier and
  termination rules to fixture measurements; does not step physics.
- `tests/historical/test_reward_incentives.py`: 13 deterministic incentive-regression tests,
  deliberately documenting current bad incentives rather than silently fixing them.
- `CMakeLists.txt`: registers the explicit classifier audit helper target.
- This report and `docs/traces/step129-incentives/`.

All 106 individual returns, B/U times, terminal contributions and assumptions are
in `summary.json` and `cases.csv`; selected per-tick timelines are preserved too.
Input/source hashes identify the objective and source traces. Tests verified
matched starting context, exact physical reward/state replay, positive prompt
recovery, delayed-recovery losses, early-fall bias, truncation semantics, bounded
extremes, telescoping shaping, lottery arithmetic and terminal-weight sensitivity.
The complete audit results reproduced exactly on a second run.

Checks passed: **41 Python tests** (13 audit, 23 balance/reward/diagnostics,
5 termination), **three C++ gates** (balance_diagnostics, balance_classifier,
termination_modes), native helper compilation, Python compilation, and
`git diff --check`. Existing NumPy-unavailable/CMake policy warnings remain
non-fatal. Protected reward, training, policy, classifier, termination,
simulation and solver source hashes are unchanged. **All 136 checkpoint hashes
are unchanged during this audit. No actor or critic training was started.**

```sh
cmake --build build-release --target aura_incentive_fixture_classifier -j4
PYTHONPATH=build-release:. .venv/bin/python -m python.diagnostics.reward_incentive_audit --output NEW_OUTPUT_DIRECTORY
PYTHONPATH=build-release:. .venv/bin/python -m unittest discover -s tests -p 'test_reward_incentives.py' -v
```

Audit stopped here. The evidence supports revising failure utility before training,
while leaving the practical feasibility and probability of active recovery open.

## Step 130 — Diagnostic absorbing-failure utility (2026-10-14)

**All 12 previously unfavorable constructed recoveries now outrank quick failure
with each of the three negative candidate utilities.** At H=1200 and gamma=.995,
all 63 recovery comparisons favor their recovery continuation for each negative
candidate. Neutral utility zero preserves all 12 old failures. This is an
objective-level diagnostic result, not demonstrated learned or dynamically
feasible recovery. No production reward, termination, physics or trainer changed.

### Failure semantics and exact indexing

Transition steps are one-based. Reset has no transition reward. The existing reward
`r_t` belongs to the physical transition ending at step t. Confirmed FALLEN ends
physical stepping exactly as before. BALANCED and UNBALANCED never trigger the
failure tail; ordinary time-limit truncation never triggers it either. A fall at
the cap can be both terminated and truncated and still has no remaining tail.

Failure utility applies only to the **virtual transitions t+1,...,H**. There are
`N=max(0,H-t)` such transitions. Their value at the first absorbing transition is:

```text
V_absorb = r_f * sum(gamma^k, k=0,...,N-1)
         = r_f * (1-gamma^N)/(1-gamma)       for gamma != 1
         = r_f * N                         for gamma == 1
```

The correction valued at the existing terminal transition is **C_t=gamma*V_absorb**.
The diagnostic may add it **once** to a copy of `r_t` before calling the unchanged
backward `discounted_returns()` recurrence. It is exactly equivalent to appending
N virtual utilities for return calculation and retaining targets only for actual
transitions. The original `r_t`, including its single -1 terminal component, is
not repeated or replaced. No virtual reward is paid on transition t itself.

From a common fork at step s (next physical reward r_(s+1) has exponent zero):

```text
G_adjusted(s) = G_original(s) + gamma^(t-s-1) * C_t
              = G_original(s) + gamma^(t-s) * r_f * sum(gamma^k, k=0,...,H-t-1)
```

Examples: first-step failure t=1 leaves H-1 virtual ticks; t=1199 leaves exactly
one value gamma*r_f at the terminal transition; t=1200 leaves none. Empty and
exceeded horizons return zero. Gamma zero discounts all future utility to zero;
gamma one gives the ordinary remaining sum. The pure `discounted_failure_tail`
function uses a stable `expm1/log` geometric series near gamma=1, handles very large
integer horizons, and rejects invalid/nonfinite inputs or numeric overflow rather
than returning an infinite value. It imports no simulator, policy or optimizer.

The failure utility is a **chosen utility of task failure**, not a measured force,
physical state property, or claim that a fallen body would naturally earn a
constant reward. Geometric-series accounting does not select that utility for us.

### Existing return/target paths inspected

`python/train_nn.py:discounted_returns()` initializes future return to zero and
runs `G_i=r_i+gamma*G_(i+1)` backwards. Both production actor collection and
`collect_critic_rollouts()` stop at terminated OR truncated; they retain exactly
one pre-action state per real reward. `prepare_critic_data()` divides discounted
returns by `CRITIC_RETURN_SCALE=100` to create critic targets. Actor reward-to-go
advantages use peer timestep baselines and the existing episode-limit scale.
These paths were inspected but not edited, run for training, or connected to the
new diagnostic. Existing `AuraEnv` still rejects production Balance V2 training.

### Candidate utility references

The controlled-standing reference earns .9995 per normal tick. Against that,
we compared the following constant failure utilities. They were derived before
checking whether they fixed the recovery grid, rather than fitted to one trace.

| Candidate            | r_f per virtual tick | Interpretation                                                                                                           |
|----------------------|---------------------:|--------------------------------------------------------------------------------------------------------------------------|
| zero                 |          0.000000000 | Failure has no ongoing utility; negative control.                                                                        |
| stationary_collapse  |         -0.400000000 | Low, tilted, unsupported failure with no excessive motion; posture and height penalty bound.                             |
| observed_collapse    |         -0.649858465 | Constant task utility anchored to pooled preterminal deep/contact-collapse median, excluding shaping and fall component. |
| physical_lower_bound |         -0.650000000 | Existing unshaped nonterminal lower bound; worst physical quality, not an extra tuned coefficient.                       |

The stationary-collapse level is the existing posture-plus-height penalty bound:
`-(.25+.15)=-.4`; it omits excessive-motion cost. The conservative physical lower
bound includes motion: `-(.25+.15+.25)=-.65`, since positive support cannot lower
unshaped reward. Recovery shaping is excluded because it already closes at the
terminal transition. Repeating that shaping or the -1 event penalty virtually
would double-count their roles.

The empirical reference is the pooled median of **363 preterminal deep/contact-
collapse ticks** across all 13 physical traces, excluding recovery and terminal
components. Its range is approximately -.649997629 to -.399969129. These ticks
are correlated and weighted by duration, not independent recovery experiments.
The median's proximity to -.65 reflects those observed collapse conditions; it
is a reference anchor, not an estimate of a uniquely correct failure utility.

### Replay of all 106 Step 129 cases

All original physical reward sequences, starting contexts and baseline returns
reproduce Step 129 exactly. All 106 cases were evaluated for every candidate (424 case/candidate results). All 63
recovery comparisons per candidate are
preserved (252 comparisons), including every previously losing case. Nothing
about the terminal transition's six physical components was changed.

| Candidate            | Previously unfavorable cases fixed / 12 | Comparisons still favoring quick fall / 63 |
|----------------------|----------------------------------------:|-------------------------------------------:|
| zero                 |                                       0 |                                         12 |
| stationary_collapse  |                                      12 |                                          0 |
| observed_collapse    |                                      12 |                                          0 |
| physical_lower_bound |                                      12 |                                          0 |

Zero utility leaves exactly the same 12 unfavorable cases from Step 129 (three
late 35-degree and nine late 55-degree recoveries). Their names and unchanged
advantages are retained in `summary.json` under `primary.zero.remaining_unfavorable`.
There are no hidden unfavorable main-grid cases for the negative candidates.

For the conservative -.65 reference, selected same-start 35-degree continuations
are shown below. Returns start at the same step-48 fork and include the same
filtered support and previous potential. Non-fallen truncated trajectories are
unchanged.

| Continuation                | Original discounted return | Failure-tail contribution | Adjusted return |
|-----------------------------|---------------------------:|--------------------------:|----------------:|
| Prompt recovery over .5 s   |                 143.405005 |                  0.000000 |      143.405005 |
| Wait 2 s then recover       |                  25.907323 |                  0.000000 |       25.907323 |
| Wait 7.5 s then recover     |                 -22.750708 |                  0.000000 |      -22.750708 |
| Quick collapse              |                 -20.598914 |               -102.826027 |     -123.424941 |
| Wait 2 s then collapse      |                 -22.843540 |                -30.595000 |      -53.438540 |
| Wait 7.5 s then collapse    |                 -23.935512 |                 -0.730139 |      -24.665651 |
| Unbalanced until truncation |                 -23.901304 |                  0.000000 |      -23.901304 |

Quick collapse ends at step 94, so 1106 virtual ticks remain. Its terminal-relative
correction is -128.844066; discounted back to the shared fork it contributes
-102.826027. The late successful recovery remains -22.750708, but now outranks
quick failure at -123.424941. For a 55-degree recovery after a four-second wait,
-31.285805 now outranks its quick-fall alternative at -123.628837.

Every one of the 12 originally losing cases is displayed here. The entries are **recovery minus quick-fall adjusted
return**; positive means recovery wins.
The complete 63-by-4 comparison table is in
`traces/step130-absorbing-failure/comparisons.csv`.

| Previously losing recovery          | Original advantage | r_f=-.4 advantage | Observed-collapse advantage | r_f=-.65 advantage |
|-------------------------------------|-------------------:|------------------:|----------------------------:|-------------------:|
| severe_recover_wait900_ramp24       |          -1.877451 |         61.400104 |                  100.926186 |         100.948576 |
| severe_recover_wait900_ramp60       |          -2.151794 |         61.125761 |                  100.651843 |         100.674233 |
| severe_recover_wait900_ramp120      |          -2.179436 |         61.098119 |                  100.624201 |         100.646591 |
| very_severe_recover_wait480_ramp24  |          -6.983644 |         56.293911 |                   95.819993 |          95.842383 |
| very_severe_recover_wait480_ramp60  |         -10.482996 |         52.794559 |                   92.320641 |          92.343031 |
| very_severe_recover_wait480_ramp120 |         -12.313811 |         50.963744 |                   90.489826 |          90.512216 |
| very_severe_recover_wait720_ramp24  |         -20.497090 |         42.780465 |                   82.306548 |          82.328937 |
| very_severe_recover_wait720_ramp60  |         -21.547906 |         41.729649 |                   81.255731 |          81.278121 |
| very_severe_recover_wait720_ramp120 |         -22.097680 |         41.179875 |                   80.705957 |          80.728347 |
| very_severe_recover_wait900_ramp24  |         -23.943976 |         39.333579 |                   78.859661 |          78.882051 |
| very_severe_recover_wait900_ramp60  |         -24.370242 |         38.907313 |                   78.433395 |          78.455785 |
| very_severe_recover_wait900_ramp120 |         -24.593260 |         38.684295 |                   78.210377 |          78.232767 |

Actual physical replays still contain only passive settling and eventual falls,
not learned recovery. For example neutral continuation from its step-80 fork
changes from +66.003788 to +51.862544 under -.65, while its geometry, classifier,
termination step 516 and reward components are identical. These changed numbers
express a different task return, not improved body control or measured learning.

### Sensitivity to gamma and horizon

We also tested diagnostic caps H=300,600,900,1200 and evaluation discounts
.99,.995,.999,1.0. Shorter caps reuse known measurements and apply the normal
Reward V2 end-potential closure at the hypothetical cap; they do not step physics,
change the production cap, or invent observations beyond the available horizon.
Cap-boundary forks occurring after the shorter cap are excluded from those
sensitivity cases. The main experiment always uses all 106 cases at H=1200.

All three negative candidates have **zero quick-fall preferences** across the
63 continuation comparisons for all 16 horizon/discount combinations. Zero
utility's remaining unfavorable comparison counts are:

| Horizon | gamma=.99 | gamma=.995 | gamma=.999 | gamma=1 |
|---------|----------:|-----------:|-----------:|--------:|
| 300     |        12 |         12 |         24 |      24 |
| 600     |        10 |         17 |         18 |      17 |
| 900     |        10 |         15 |         11 |      10 |
| 1200    |        10 |         12 |          3 |       2 |

Two qualifications are essential. At short horizons some named recovery
continuations have not yet recovered: a favorable rank there may mean *remaining
UNBALANCED until the cap* beats falling, not that recovery completed. Second,
gamma sensitivity reprices unchanged Reward V2 samples whose shaping discount
remains .995. Potential-shaping invariance is not guaranteed at the other
comparison gammas. This is an evaluation-utility sensitivity study, not a proposal
to change training gamma without updating/validating the shaping convention.

### Interpretation and recommended next candidate

The negative absorbing outcomes remove the tested advantage of escaping future
negative quality by ending an episode. They do **not** award recovery directly:
recovery's original physical return is unchanged. The failure outcome is now
valued over the same remaining time as continuation.

For a subsequent versioned experiment, **-.65 is the clearest conservative
candidate**: it comes from the current nonterminal physical lower bound and is
close to the independent pooled collapse reference. It is not selected because
-.4 failed this grid; -.4 also fixes every tested case. However -.4 is higher
than some valid negative-quality living rewards, so it offers no general
protection against escaping worse-than-.4 future quality. The lower-bound
argument addresses that specific escape mechanism, not all choices between
physically different trajectories or all possible optimal policies.

The effective cost is substantial despite its interpretable per-tick scale.
For gamma=.995 the terminal-relative infinite-tail magnitude is bounded by
`.65*.995/(1-.995)=129.35`; H=1200 gives a smaller finite value. Concentrating this
future value into one return correction can still affect gradient variance and
critic-target range like a large terminal cost. Its time dependence is justified
by the chosen finite task utility, but that does not make its learning behavior
automatically benign. The unchanged critic target convention would scale raw
returns by 100; that scaling must be checked before any later training.

### Future production integration strategy — not implemented

1. Review the chosen failure utility together with richer balance/recovery metrics.
   Approve a new **return-objective version** separately from physical Reward V2.
   Store utility, H, gamma, indexing and absorbing-model version in checkpoint and
   experiment metadata; historical critics cannot be treated as calibrated for
   this new return merely because their input/output shapes match.
2. Keep physical stepping and raw Reward V2 components unchanged. On confirmed
   FALLEN, compute the tail once from the actual terminal step. Expose physical
   reward, terminal-relative tail and total learning-return objective separately;
   do not overwrite component diagnostics so their sum becomes misleading.
3. Add the correction to a *copy* of the final reward in return preparation before
   both actor advantages and critic targets are computed. Reuse the existing
   backward recurrence. Never create virtual observations/actions/log-probabilities
   or weight the actor loss with extra virtual samples. There remain exactly as
   many targets as actual pre-action states. Tests show equality with an explicitly
   expanded virtual reward sequence at every actual timestep.
4. Ordinary truncation gets no tail; a simultaneous FALLEN/cap event has zero
   remaining ticks and keeps its original physical terminal reward. Update every
   relevant collection/experiment path consistently before enabling an opt-in V2
   trainer. Do not mix a tail-adjusted actor objective with an old legacy critic or
   an unadjusted timestep baseline.
5. Fit and validate a fresh critic/baseline for the versioned objective only after
   approval. Preserve current 205 observations and 32 actions during this review.
   Remaining time and support-filter memory are not explicit in the unchanged
   neural state; finite-horizon state-value aliasing needs an explicit decision,
   not a silent architecture change. The audit makes no claim this is resolved.
6. Run controlled matched-seed learning comparisons and actual attainable recovery
   trials, with balance-quality metrics alongside survival. Keep production legacy
   until those gates pass. No part of this integration was performed here.

### Known limitations and possible new incentives

- All recovery comparisons are constructed measurement trajectories; actuator,
  joint, contact and force feasibility remains unproven. Native recovery is passive
  resettling. Removing an objective counterexample is not proof of better learning.
- A body that remains UNBALANCED until truncation avoids absorbing failure utility.
  The 35-degree fixture stays at -23.901 and the 55-degree fixture at -46.334,
  now above quick failure. This fixes the early-exit problem but can make stalling
  or threshold avoidance attractive. It does not establish genuine stable balance.
- A confirmed fall just beyond the time cap avoids the tail and event cost, while
  a fall at the cap retains the event cost. This finite-horizon boundary remains;
  it is why recovery success, balanced duration and controlled motion must be
  evaluated independently of episode survival.
- Geometric contacts are not measured load bearing. Hand/wall support, solver pose
  correction and rigid marginal postures remain possible reward weaknesses.
- Candidate utilities are normative task choices. The empirical median is a
  correlated diagnostic reference; the -.65 bound is conservative rather than a
  uniquely physically correct failure value. Other reward versions would require
  recalculating/reviewing these references.
- Longer-horizon learning, different starts, real action trajectories and learned
  recovery probabilities remain untested. Zero losses in this fixed grid is not a
  universal policy-level guarantee.

### Files, tests, preservation and reproduction

Added `python/failure_tail.py`, `python/diagnostics/absorbing_failure_audit.py`,
`tests/historical/test_failure_tail.py`, and `tests/historical/test_absorbing_failure_audit.py`, this report
and `docs/traces/step130-absorbing-failure/`. No existing reward, training or
termination file was edited. The existing native fixture classifier was reused;
no physical simulation was required to construct any failure tail.

Passed **52 Python tests**: 10 pure tail tests, nine full audit tests, 13 Step 129
incentive tests, 15 existing reward tests and five termination tests. Three C++
gates passed: balance_diagnostics, balance_classifier, termination_modes. Python
compilation and `git diff --check` passed. The full candidate/sensitivity replay
reproduced exactly, and all 106 original objective results remain exact. Tests
cover geometric sums, indexing, gamma one/zero, empty/overshot horizons, ordinary
truncation, cap failure, finite outputs, overflow rejection, explicit expanded-
return equivalence and all recovery comparisons. Existing NumPy warning is
non-fatal; no NumPy-dependent calculation or neural training was performed.

Protected production source hashes and all 136 historical/active checkpoint
hashes are unchanged during this step. No actor or critic training was started.
See `verification.json` for the preservation check. The remaining hashes in
`summary.json` identify the exact diagnostic and original objective sources.

```sh
PYTHONPATH=build-release:. .venv/bin/python -m python.diagnostics.absorbing_failure_audit --output NEW_OUTPUT_DIRECTORY
PYTHONPATH=build-release:. .venv/bin/python -m unittest discover -s tests -p 'test_failure_tail.py' -v
PYTHONPATH=build-release:. .venv/bin/python -m unittest discover -s tests -p 'test_absorbing_failure_audit.py' -v
```

Stopped after diagnostic implementation and evaluation. Production Reward V2,
legacy defaults, physical termination and network training remain unchanged.

## Step 131 — Unbalanced stalling incentive audit (2026-10-14)

**The failure tail prevents early termination from escaping future cost, but it
still makes a nonterminal stall more valuable than confirmed collapse.** The
largest observed issue is discounting: at gamma=.995, a constructed slow collapse
that never reaches FALLEN has a discounted return of +25.125 even though its
undiscounted reward is -295.402. A delayed recovery after six seconds earns
-5.740 discounted and +290.021 undiscounted. These are measurement fixtures,
not evidence of a policy exploit, but they show the current return can prefer
late deterioration over late recovery.

### Method and reused evidence

The audit ran the unchanged `BalanceRewardV2` implementation, the existing
Step 130 failure-tail function, gamma=.995, horizon 1200 and the current C++
balance classifier. It used all 13 Step 128 passive physics traces, four selected
Step 129 constructed cases (re-scored exactly against their archived results),
and seven additional measurement fixtures. The Step 130 saved verification record
was checked and reports all 106 prior objective cases exact. The new run did not
replay all 106 cases; it directly re-scored the four Step 129 fixtures it uses.

The new fixtures define measured posture, height, support, contact, and rate
signals, then ask the existing classifier to label every tick. They do not
invent reward numbers. The crouch is supported and upright at 62% of neutral
height. Near-balance oscillation moves between 14 and 26 degrees around the
classifier's entry and retention envelopes. Near-fall oscillation moves between
59 and 61 degrees while keeping collapse-height ratios low, so it tests the
classifier's 60-degree condition and persistence. Slow collapse ramps over two
seconds to 55 degrees and 40% neutral height, then stays below the collapse
angle. A confirmed-fall fixture uses the actual classifier's FALLEN transition.
These paths are prescribed measurements, not force-, contact-, or
action-feasible controller episodes.

Step 128 native neutral and small-pitch traces remain passive zero-target
simulation, not learned recovery. They provide measured physical examples but do
not demonstrate a controller can reproduce any constructed recovery.

### Main results

Returns below use the Step 130 physical-lower-bound candidate `r_f=-.65`.
Discounted returns use gamma=.995 from episode start; undiscounted adjusted
returns include the failure utility on virtual remaining ticks only for FALLEN.
All cases share the 1200-step task cap, though physical fall ends the recorded
trajectory earlier. Mean quality columns are physical Reward V2 measurements,
not classifier scores.

| Scenario                           | Discounted adjusted return | Undiscounted adjusted return | Balanced / unbalanced time | Mean posture | Mean motion penalty | Mean support | Failure tail at start |
|------------------------------------|---------------------------:|-----------------------------:|---------------------------:|-------------:|--------------------:|-------------:|----------------------:|
| Controlled standing                |                   +199.412 |                    +1199.300 |             9.86 s / .14 s |        1.000 |                .000 |        1.000 |                     0 |
| Prompt recovery                    |                   +122.756 |                    +1097.200 |             9.01 s / .99 s |         .974 |                .053 |         .979 |                     0 |
| Delayed recovery                   |                     -5.740 |                     +290.021 |            3.01 s / 6.99 s |         .528 |                .053 |         .529 |                     0 |
| Constant 35° unbalanced            |                     -8.772 |                     -130.417 |             .05 s / 9.95 s |         .276 |                .020 |         .277 |                     0 |
| Supported crouch                   |                   +129.118 |                     +892.667 |             .05 s / 9.95 s |         .975 |                .045 |        1.000 |                     0 |
| Constant supported 19° posture     |                   +128.805 |                     +553.059 |             9.86 s / .14 s |         .696 |                .000 |         .764 |                     0 |
| Oscillation near balanced boundary |                    +40.618 |                     +220.008 |             .85 s / 9.15 s |         .674 |                .323 |         .746 |                     0 |
| Constant 59° near-fall stall       |                    -34.185 |                     -425.530 |             .05 s / 9.95 s |         .099 |                .110 |         .135 |                     0 |
| Oscillation near FALLEN boundary   |                    -34.232 |                     -426.169 |             .05 s / 9.95 s |         .097 |                .110 |         .135 |                     0 |
| Slow collapse, no FALLEN           |                    +25.125 |                     -295.402 |             .05 s / 9.95 s |         .138 |                .058 |         .601 |                     0 |
| Confirmed collapse                 |                    -87.013 |                     -732.335 |              .05 s / .73 s |         .390 |                .511 |         .551 |               -80.837 |

The passive native traces provide context: neutral ended at step 516 with adjusted
discounted return +50.636; the passive small-pitch recovery attempt ended at step
368 with +22.824. Neither is a learned recovery. Their nonzero starting values
reflect their short-lived initial upright/support rewards before falling.

### Incentive findings

1. **Stalling below FALLEN is systematically better than confirmed failure in
   these fixtures.** With `r_f=-.65`, a near-fall stall scores -34.185 versus
   -87.013 for a confirmed collapse. The stall receives no failure tail because
   it truncates while UNBALANCED. The ordinary terminal penalty is only -1.000
   undiscounted (-.627 discounted in this fall); the tail contributes -80.837
   from the common start. This is the intended failure-versus-continuation
   distinction, but it gives an explicit incentive to avoid the FALLEN boundary
   without recovering.
2. **The tested boundary oscillations do not pay.** Near-FALLEN oscillation
   returns -34.232, slightly below the constant near-fall hold at -34.185. The
   near-BALANCED cycle returns +40.618, below a stable supported 19-degree pose
   at +128.805 and controlled standing at +199.412. The measured cycles do not
   show reward farming by switching states; their interpolated feasibility is
   still unknown.
3. **The classifier label is not the reward target.** A controlled supported
   crouch remains UNBALANCED because its height misses the standing envelope,
   yet receives +129.118, below standing but far from being punished as a fall.
   This supports using physical measurements alongside labels.
4. **Gamma changes the practical ranking.** For the constant near-fall stall,
   discounted return is +.726 at gamma=.99, -34.185 at .995, -228.308 at .999,
   and -425.530 undiscounted at 1.0. For confirmed collapse with `r_f=-.65`,
   the corresponding values are -26.363, -87.013, -407.767, and -732.335.
   Increasing gamma accumulates more of the stall's negative physical rewards,
   but the failure remains worse because its tail also grows. With the milder
   `r_f=-.40` candidate, the confirmed-fall return is -55.922 at .995, still
   below the -34.185 stall. The Step 130 failure utility alone does not remove
   the stalling preference across the tested choices.
5. **Discounting can hide late deterioration.** The slow-collapse fixture has
   strongly negative undiscounted reward but positive discounted return. At
   .995 its early upright/support rewards outweigh much of the later decline.
   This is not a manually assigned reward sequence: it is the actual reward
   applied tick by tick to the prescribed measurement path. It shows why the
   two return measures and late-state metrics must both be reviewed.

The broad order is controlled standing > prompt recovery > prolonged instability
> confirmed failure for the selected comparisons. The six-second delayed
> recovery and slow-collapse fixtures are counterexamples to a universal ordering;
> the former is late and the latter never completes recovery. Synthetic returns
> cannot establish which motions a trained actor can produce.

### Recommendation, limitations, and verification

Do not integrate this objective into actor or critic training yet. Keep the
Step 130 tail diagnostic-only. Before a new reward version, test whether
prolonged UNBALANCED states should receive a bounded time cost, whether reward
should explicitly value physical progress toward recovery, and how that cost
preserves controlled crouching and temporary support loss. Those choices need
matched feasible action rollouts; changing a penalty to force the desired table
would overfit these fixtures. Continue reporting posture, uncontrolled motion,
support, balanced duration, recovery success, undiscounted reward, discounted
return, and failure-tail contribution separately.

No Reward V2 coefficient, failure utility, classifier, termination logic,
physics, neural schema, trainer, or checkpoint writer was changed. No actor or
critic training ran. The measurement audit itself used only saved physical traces
and the fixture classifier; it advanced no new physical simulation. C++
regression tests did include simulation steps. Nine Step 131 Python tests passed;
the relevant Python regression batch passed 72 tests, including Reward V2,
Step 129/130 audits, termination, classifier, and post-termination behavior.
The C++ `balance_diagnostics` and `balance_classifier` gates passed. The C++
`termination_modes` test failed its existing neutral-end expectation (`legacyEnd==462 && v2End==516`); the test source
and termination implementation
were not changed in this step, so that discrepancy remains unresolved and means
the C++ legacy timing expectation is not verified here. The current workspace
contains no `.pt` checkpoint files, so there were no local checkpoint hashes to
compare; protected production source hashes stayed unchanged. A benign PyTorch
warning reports NumPy is unavailable.

The reproducible implementation, scenario measurements, gamma/utility sweep,
and verification record are in `python/diagnostics/unbalanced_stall_audit.py`,
`tests/historical/test_unbalanced_stall_audit.py`, and `traces/step131-unbalanced-stalling/`.
No diagnostic conclusion here enables training.

## Step 132 — Termination regression investigation (2026-10-14)

**The failed test used a Debug simulation with historical Release endpoint
expectations.** Current Release still terminates at legacy step 462 and V2 step
516, and reproduces the archived Step 128 cumulative physical-state digest
`efa666e369d7f1e3` exactly through step 642. Current Debug reproducibly terminates
at 455 and 530. Fresh independent builds of the same current source reproduce
both trajectories. This is a build-dependent numerical reproducibility issue;
the investigation found no neutral Release physics regression against the archive.

### Implementation and build inspected

- `tests/simulation/termination_tests.cpp` checks mode truth tables, cap precedence, raw
  stepping, and literal neutral endpoints 462/516. The same test is registered
  in both Debug and Release. It does not declare that its numeric trace reference
  was measured in Release. The Debug failure occurs at the final exact-endpoint
  assertion, after the preceding policy/API consistency assertions succeed.
- `src/sim/AuraSimulation.cpp` constructs the body and skeleton, creates 105
  eligible collision pairs, enables the normal motor policy, and raises all body
  parts by .6 m. Raw `step()` writes the current action targets, calls the shared
  application step, increments time/step once, and updates the classifier once.
  Raw stepping does not stop merely because a termination flag becomes true.
- `src/app/SimulationStep.cpp/.hpp` use 1/120 s, 16 solver iterations, gravity
  9.81, the existing forward/reverse passes, self-collision, and final room pass.
  No timestep, gains, damping, budgets, ordering, or contact settings changed.
- `src/sim/BalanceClassifier.cpp` retains the same standing envelopes and
  18-tick entry, six-tick exit and 24-tick collapse persistence. It reads physical
  measurements and does not feed corrections back into physics.
- `include/aura/sim/Termination.hpp` maps legacy to `!isStanding()` and V2 to
  `balance_state==FALLEN`; time-cap truncation is independently `step>=1200`.
  No termination mode branches inside the physical stepping routine.
- `tests/simulation/runtime_baseline_tests.cpp` tests motor contracts and geometric/finite
  bounds. Its direct application-step replay retains configured hip/knee targets;
  the neutral `AuraSimulation` replay overwrites action-mapped targets with zero
  at every step. These are existing distinct configurations. Its direct replay
  must not be substituted for the neutral endpoint test.
- `build/CMakeCache.txt` selects Debug (`-g`, default O0); `build-release` selects
  Release (`-O3 -DNDEBUG`). Both use Apple Clang 21.0.0 on arm64 with no configured
  fast-math flags. No `NDEBUG`-conditional or assert-dependent behavior was found
  in the relevant math, physics, body, simulation, and application libraries.

The worktree contains earlier articulation changes relative to Git HEAD, while
the archived experiment was recorded after those changes. Current file hashes
also differ from archived source hashes, so the history is not a complete frozen
source snapshot. The evidence for baseline continuity is the exact current
Release physical trace digest, not an assumption that every historical source
edit was cosmetic. The only outstanding `src/physics/CollisionShape.cpp` diff is
spacing around an array template; its whitespace-insensitive diff is empty.

### Reproduction and repeatability

`tests/tools/termination_regression_probe.cpp` is a new diagnostic executable source.
It uses the test's default construction and paired stepping, checks finite state,
hashes every part's position, orientation, velocity, acceleration, angular state,
force and torque, and records classifier/termination measurements. It continues
both modes through 642 raw ticks to match the archived diagnostic duration.
There are no controller actions, training updates, or physics changes.

| Build | Replays | First BALANCED | First legacy termination | First FALLEN / V2 termination | Physical digest at 642 |
|---|---:|---:|---:|---:|---|
| Existing Debug | 3 | 64 | 455 | 530 | `62b6bcaf3d24ab08` |
| Existing Release | 3 | 64 | 462 | 516 | `efa666e369d7f1e3` |
| Fresh isolated Debug | 1 | 64 | 455 | 530 | `62b6bcaf3d24ab08` |
| Fresh isolated Release | 1 | 64 | 462 | 516 | `efa666e369d7f1e3` |
| Diagnostic Release, contraction disabled | 1 | 64 | 463 | 520 | `eb1d9ed5df32d9ab` |

Within each existing build, all three CSV files are byte-identical. Fresh-build
CSV files exactly match their existing-build counterparts. In every replay,
legacy and V2 physical-state hashes agree at every tick, including after their
termination flags differ. This rules out termination-mode-dependent stepping as
the cause for this neutral case. Fresh builds rule out stale objects as the
explanation of the Debug/Release difference.

The initial physical states and complete motor/configuration snapshots match
across Debug and Release: 105 pairs, 14 enabled motors, head motor disabled, and
identical gains. The configured hip/knee targets are .5/.6 rad before first step;
the zero current action overrides those mapped targets at each physical step in
both builds. Both first touch the floor at tick 42, become BALANCED at 64, and
leave BALANCED at 210.

The first physical-state hash difference appears at tick **146**. The reported
COM vertical velocities are -0.15389852225780487 in Debug and
-0.15389853715896606 in Release, a difference of about 1.49e-8 m/s. Later contact
and iterative constraint outcomes diverge. The existing solver is sensitive to
these small numerical differences during the fall; fixed-step repeatability
within one build does not imply bit-identical trajectories across optimization
settings.

An isolated `-O3 -DNDEBUG -ffp-contract=off` diagnostic build gives 463/520.
Floating-point contraction controls whether eligible multiply/add expressions
may be combined with a single rounding. Its changed result confirms sensitivity
to compiler floating-point evaluation choices. It does **not** reproduce the
Debug trace, so the investigation does not identify one fused instruction as the
entire cause. No production compiler flag was changed. Instruction-level
attribution and universal cross-toolchain determinism remain outside this result.

### Measurements at the transitions

Legacy standing requires pelvis height above half its initial 4.77 m height
(about 2.385 m) **and** pelvis up-dot-Y above .5. The two builds cross different
parts of that predicate first:

| Event | Step | Pelvis height | Pelvis up-Y | Torso height | Torso up-Y | Foot contacts L/R | Collapse condition |
|---|---:|---:|---:|---:|---:|---|---|
| Debug legacy end | 455 | 3.154175 | .490486 | 3.656300 | -.054983 | yes / yes | None; legacy tilt condition fails |
| Release legacy end | 462 | 2.375136 | .549118 | 3.194922 | .293808 | no / yes | None; legacy height condition fails |
| Debug V2 end | 530 | 1.687186 | -.547919 | .943856 | -.190178 | no / no | Contact collapse, sustained 24 ticks |
| Release V2 end | 516 | 1.129496 | -.064333 | .901196 | -.216845 | no / no | Deep collapse, sustained 24 ticks |

At Debug legacy termination COM height is 2.258876 m with vertical velocity
-5.827524 m/s and support margin -1.948308 m. Release legacy termination has
COM height 1.942835 m, vertical velocity -8.098733 m/s and a degenerate one-point
support polygon. At V2 termination the Debug COM height is 1.590800 m and
Release COM height .991387 m; both have no foot support polygon. The recorded
collapse duration is .19999999999999998 s, accepted by the unchanged classifier's
floating-point comparison allowance. The different fall times follow different
physical trajectories, not a change in the persistence rule.

### Conclusion and smallest recommended correction

The historical **462/516 values remain correct for the validated Release build**.
They are not universal Debug or compiler-independent constants. Thus the test's
build-independent use of that historical reference is incorrectly scoped;
blindly replacing its numbers with 455/530 would break the valid Release replay.

The smallest immediate correction to the validation workflow is to run the
archived endpoint regression using `build-release`. For a later test cleanup,
keep general termination truth tables, cap/latch/reset semantics and paired-mode
physics checks independent of fixed historical endpoints. Keep the exact
462/516 trace regression as a separately identified Release/toolchain reference,
with build flags and source/trace hashes recorded. This does not require changing
the solver, reward, classifier or fall criteria. No existing test expectation or
production build setting was edited in Step 132.

Tests/results: isolated Debug `termination_modes` reproduces the final endpoint
failure; Release `termination_modes` passes the entire original test, including
its later latch/reset assertions. The fresh isolated Release test also passes.
`balance_classifier` passes in both normal Debug and Release builds. Nine paired
diagnostic replays completed with exact mode equality at every sampled tick.
The protected-source manifest confirms all existing production files remain
unchanged during the investigation. No actor or critic training was run.

The report data, every repeated CSV, configuration snapshots, source-preservation
record and the isolated build-project snapshot are in
`traces/step132-termination/`. The isolated project builds only the three existing
math/physics/simulation libraries, the unmodified termination test and the new
probe, so it requires no viewer or Python dependencies:

```sh
cmake -S docs/traces/step132-termination/clean-project -B /tmp/aura-termination-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/aura-termination-release -j4
/tmp/aura-termination-release/probe /tmp/aura-termination-release-traces
/tmp/aura-termination-release/termination_test
```

Use Debug in a separate output directory to reproduce the alternate trajectory.
The contraction-off build is diagnostic evidence only. Keep reward and learning
work anchored to a recorded, consistent build; this investigation does not
establish physically feasible recovery or repair Reward V2's incentives.

## Step 133 — Portable termination checks and strict Release replay (2026-10-14)

The termination suite now separates policy behavior from the Step 128 numerical
reference. Production physics, classification, reward, termination, action and
observation code remain unchanged.

| CTest name | Configurations | Purpose |
|---|---|---|
| `termination_modes` | Debug and Release | Termination behavior and API contracts; no historical endpoint assertions |
| `balance_classifier` | Debug and Release | Existing classifier persistence, hysteresis, latching and observation integration checks |
| `termination_release_replay` | Release only | Exact archived neutral measurements, 462/516 endpoints, and original physical-state digest |

`tests/simulation/termination_tests.cpp` keeps the existing termination truth table and cap
precedence checks. During paired default simulations it now checks legacy's
original standing predicate, V2 termination exactly on FALLEN, UNBALANCED
continuation, consistent flags and step counts, unchanged physical state between
modes, idempotent observations, FALLEN latching through subsequent raw steps,
and reset of both modes' state, step count and clock. It observes a recovery
window after legacy failure without tying that window to tick 462. Neutral runs
must exercise both termination paths before the cap, but their observed endpoint
numbers are printed as diagnostics rather than used as historical assertions.

`tests/historical/termination_release_replay_tests.cpp` retains the strict expected legacy
462 and V2 516 endpoints. It compares 31 numeric fields, classifier state and
termination reason at every one of the 643 Step 128 samples (reset through tick
642) against `docs/traces/step128-physical/neutral.csv`. All numeric comparisons
are exact, including contact/support signals and classifier timers. It also
checks the original cumulative physical-state digest `efa666e369d7f1e3`, covering
positions, orientations, velocities, accelerations, forces and torques for all
parts. No archived data or tolerance was changed. The archive's displayed time
uses tick * dt, so that field retains the archive's original calculation.

`CMakeLists.txt` labels the behavioral and historical tests separately. A
single-config Debug build does not create/register the Release replay target.
A single-config Release build registers it for ordinary `ctest` without needing
`-C`. Multi-config generators restrict its registration to `ctest -C Release`;
the executable additionally guards against running the numerical replay from a
non-Release compilation. This registration logic does not change production
library compiler flags. The historical reference remains the validated Apple
Clang 21 / arm64 Release configuration; another compiler or floating-point policy
may fail the strict reference and requires investigation, not a relaxed baseline.

Validation passed: Debug behavioral termination and classifier tests; Release
behavioral termination, classifier and strict historical replay tests. The
behavioral neutral runs observed 455/530 in Debug and 462/516 in Release. The
strict Release test matched all 643 samples and the original digest. A temporary
copy with COM vertical velocity changed by only 1e-9 at tick 146 was rejected at
that exact field/tick, confirming that the replay has no loosened numeric
tolerance. The original archived trace was never edited. No remaining failures
were found in the scoped tests. Multi-config registration is implemented but the
executed builds were the existing single-config Ninja Debug and Release builds.

Reproduction:

```sh
cmake --build build --target aura_termination_tests aura_balance_classifier_tests -j4
ctest --test-dir build --output-on-failure -R '^(termination_modes|balance_classifier)$'
cmake --build build-release --target aura_termination_tests aura_balance_classifier_tests aura_termination_release_replay_tests -j4
ctest --test-dir build-release --output-on-failure -R '^(termination_modes|balance_classifier|termination_release_replay)$'
```

All existing production-source fingerprints were preserved. No actor or critic
training ran and no historical checkpoint was read or written. The cleanup stops
at test infrastructure; feasible recovery experiments remain the next research
task, and Reward V2 remains diagnostic-only.

## Experimental balance quality — first Reward V3 building block (2026-10-14)

`python/balance_quality.py` defines pure `balance_quality(observation)` and
`balance_quality_components(observation)`. This is quality version 1, not a
registered environment reward. It reads existing physical measurements, returns
values in [0,1], and never mutates observations or uses classification labels,
episode flags, elapsed time, neural networks, or a history buffer. Missing fields
fail explicitly; nonfinite measurements and invalid settings are rejected.

Five qualities are multiplied so a strong height/support score cannot cancel
uncontrolled body motion. Posture is the arithmetic mean of the pelvis and torso
qualities, as proposed. For each part, tilt theta is acos(uprightness), and its
quality is exp(-(theta / 30 degrees)^2). This is a smooth diagnostic scale, not a
termination angle. Height is the geometric mean of two smoothstep scores based
on each part's neutral height: zero at/below 25%, full at/above 75%. This plateau
allows controlled crouching and gives no extra credit for artificially increasing
height. Deeper crouches receive partial quality; this is an unvalidated task
preference, not a claim about their physical controllability.

COM motion quality is exp(-(vertical_excess^2 + horizontal_excess^2)). Excess
speed starts above 0.25 m/s vertically and 0.35 m/s horizontally, normalized by
0.25 sqrt(g * neutral_COM_height). Rotation quality is exp(-mean(omega_excess^2))
for pelvis and torso, with a 0.4 rad/s deadband and scale
0.5 sqrt(g / neutral_COM_height). These preserve Reward V2's existing initial
diagnostic scales, rather than fitting new coefficients to incentive cases.
COM acceleration is not exposed here and is not inferred from a single sample.
Independent limb/head motion is not penalized directly; its effects on COM and
main-body motion remain visible. The motion terms still penalize fast controlled
turning, so this is specific to standing balance, not a general movement reward.

Support is zero without either grounded foot. With geometric foot contact but
no valid support hull, it is 0.25. With a hull, it is
0.25 + 0.75 smoothstep((margin / 0.1 m + 1) / 2). One foot can earn full quality.
Outside-support states retain marginal quality and are never declared terminal
by this function. The hull is assumed to be the simulator's convex hull, which
removes collinear points; this helper is not a replacement hull constructor.
These contact fields describe floor proximity, not measured load bearing:
`src/python/AuraPython.cpp` does not expose ground reaction impulses. Consequently
the support term is explicitly a geometric proxy. It cannot prove genuine support.

There is no contact smoothing in an instantaneous pure function. A brief airborne
sample scores zero but cannot end an episode. Temporal persistence stays with the
existing classifier; any future quality smoothing needs a separate explicit
stateful design. FALLEN and truncation handling also belong to a future reward
adapter. Even an observation relabeled FALLEN retains its physical quality here;
zero-utility absorbing failure must be imposed by that adapter, not inferred by
silently mixing labels into the physical score.

Validation includes 11 new tests plus all 15 existing Reward V2 tests (26 passed).
Checks cover component/product semantics, posture symmetry and ordering, crouch
plateau, motion deadbands, severe motion, one-foot/airborne/degenerate support,
purity and label independence, invalid inputs, finite extreme speeds, and the
archived physical collapse. Measurement fixtures produce:

| Measurement fixture | Quality |
|---|---:|
| Upright, supported, controlled | 1.0000 |
| Both main parts tilted 10 degrees | 0.8948 |
| Both tilted 30 degrees | 0.3679 |
| Both tilted 60 degrees | 0.0183 |
| Controlled crouch at 75% neutral height | 1.0000 |
| Crouch at 50% neutral height | 0.5000 |
| One supporting foot, ample margin | 1.0000 |
| Both feet airborne | 0.0000 |
| Upright but COM beyond support by 0.2 m | 0.2500 |

These fixtures specify measurements; they do not establish attainable articulated
poses, recovery trajectories or sustained motor control.

`python/diagnostics/balance_quality_audit.py` replays all 13 archived Step 128 physical traces
through termination, without physics stepping or training. Output is saved under
`docs/traces/balance-quality-v1/`: each CSV contains all five components, and
`summary.json` contains source hashes and grouped descriptive statistics. Reset
and terminal samples are included for inspection, so these averages are not
learning returns. The neutral trace scores 0 at the initially airborne reset,
0.99994 at tick 100, 0.21097 at first UNBALANCED tick 210, 0.00467 at tick 300,
and 0 at confirmed FALLEN tick 516. Its BALANCED samples average 0.89609; the
archived recovery-attempt trace's BALANCED samples average 0.92144. Every trace
ends with physical quality zero. A second full replay is byte-identical, and the
input archives remain unchanged. These traces include passive settling and
failed recovery attempts; they do not prove active recovery from motor commands.

Remaining research gates: multiplicative scoring can make the learning signal
very small; averaging posture can tolerate one badly oriented main part; positive
marginal support can still reward stalling; geometric contact may misidentify
load bearing; and discounting can still rank early partial quality above delayed
recovery. Nonnegative quality prevents accumulating negative recovery debt, but
does not establish desirable return rankings between different physical paths.
The 106-case return audit, stalling comparisons, feasible motor recovery trials,
and sensitivity analysis are not claimed complete by this component validation.
No candidate has been cleared for training.

Candidate A remains the next reward-adapter experiment. Candidate B is not
implemented. If introduced, potential shaping must be
beta * (gamma * Phi(next) - Phi(current)), with beta multiplying the entire
difference, and explicit terminal/truncation/finite-horizon semantics.

Reproduction:

```sh
PYTHONPATH=build-release:. .venv/bin/python -m unittest tests.learning.test_balance_quality tests.historical.test_balance_reward_v2 -q
PYTHONPATH=. .venv/bin/python -m python.diagnostics.balance_quality_audit docs/traces/step128-physical /tmp/aura-quality-new-replay
```

Legacy/V2 rewards and termination, physics, observations, actions, training code,
and historical checkpoints are unchanged. No training or new simulation ran.

## Primary Balance System integration (2026-10-15)

This supersedes the diagnostic-only status of the quality adapter above, at the
developer's explicit request. It does not certify the reward as optimal. New
native simulations, Python environments, CLI training sessions, manual Balance
Lab simulations, and viewer classification use the existing whole-body classifier
by default. BALANCED and UNBALANCED continue; confirmed FALLEN terminates; the
1200-step cap truncates. Existing flag precedence is preserved when both endings
coincide. `isStanding()` and explicit legacy termination remain intact for replay.
Changing the default mode does not change physical initialization or stepping.

`AuraEnv.step()` pays `balance_quality(next_observation)` for nonterminal
transitions, including an ordinary time-limit transition, and zero on confirmed
FALLEN. After either episode ending, stepping raises before calling physics.
Reset is required to collect any further reward. The environment retains physical
quality components independently of the paid terminal reward. Reward V2 remains
available in the historical diagnostic wrapper; none of its negative penalties,
potential shaping, terminal penalties, or the -0.65 absorbing utility enter the
active trainer. Future utility beyond failure and the finite task horizon is zero.

The active contract is **environment version 3 / reward version 3 (balance_quality)
/ quality function version 2 / termination version 2 (balance_v2)**. It records
the complete quality configuration, gamma 0.995, horizon 1200, zero terminal and
absorbing utilities, and finite-horizon truncation semantics. Observation schema
2 (205 values), action schema 5 (32 outputs), action transforms, actor and AuraValue
architectures, exploration, physics, and classification thresholds are unchanged.

### Learning and persistence

The existing Monte Carlo rollout/return and critic-fitting infrastructure is reused.
New sessions initialize the actor as before and a fresh AuraValue critic (205 / 64
/ 64 / 1, Tanh hidden layers, linear value output), using seed actor_seed + 1 in an
isolated RNG context. Historical residual or direct critics are never implicitly
loaded. There is no transferred timestep table or residual baseline in this path.

For each batch, the critic predicts pre-action state values before fitting that
batch. Predictions are detached for the actor loss. Returns use the existing
discounted-return calculation, with no bootstrap beyond confirmed failure or the
finite task horizon. The critic target is G/100; the actor's step advantage is
(G/100 - V) * 100/1200, equivalent to (G - 100 V)/1200. The policy loss retains the
existing sum over action timesteps divided by episode count and uses probabilities
of the original unclipped Gaussian samples. The executed motor targets retain
their existing clipping/scaling. Actor and critic have disjoint parameters and
separate Adam optimizers; each receives one update per requested training batch.
The critic is fitted after the policy gradient is formed, avoiding a baseline
that has already fitted its own current-batch return targets. Update flags permit
critic-only or actor-only verification without changing the other network.

Checkpoint container format stays 1 for the existing readers, with additive
contract and `training_algorithm=actor_critic_mc` metadata. A new checkpoint must
include the paired critic, its architecture and return scale, its matching full
objective contract, and its optimizer state. Actor and critic are atomically saved
in one checkpoint file together with actor Adam history and exploration RNG.
Resume restores both networks/optimizers and the saved seed. Tests verify that
the next resumed actor/critic update exactly matches uninterrupted fixture
training. Incompatible objectives, quality configuration, schema or critic
contracts reject resume with a message to create a new model. Restart also refuses
legacy objectives, protecting old results from being overwritten by the new task.
Best files remain read-only for training. Historical experiment entry points now
explicitly select LegacyAuraEnv or native legacy mode, preserving their original
rewards and termination rather than silently following the changed default.

### Measurements and CLI

`python/balance_metrics.py` counts post-transition classifications, excluding
reset. It reports balanced steps and duration, unbalanced steps, longest continuous
balanced interval in steps, balanced fraction, recoveries, per-episode fall
indicator, average physical balance quality, total paid reward, and end reason.
Recovery requires BALANCED followed by actual UNBALANCED and a later BALANCED
transition; initial airborne settling/landing never counts as recovery. Quality
averages include terminal physical measurements, while the paid terminal reward
is zero. Reward, physical classification, and metrics remain separate concerns.

Training and `/evaluate` collect one deterministic and one seeded stochastic
episode per evaluation, without optimizer updates or changing exploration RNG.
Each result reports both modes separately; a one-episode fall rate is a binary
indicator, not a reliable population estimate. Deterministic best selection uses
balanced steps, then total reward; survival remains a diagnostic and is not the
selection authority. Evaluation protocol version 4 includes the environment
contract, preventing historical scores from being compared as the same objective.

The CLI Learning page displays training and both evaluation modes, the quality
reward semantics, critic MSE and scaled value targets. The policy-gradient panel
shows the detached value baseline instead of the old leave-one-out timestep
baseline. Overview emphasizes evaluated balanced steps and the best balanced
duration. Home labels identify paired actor–critic learning. Model inspection
identifies objective and paired critic architecture; incompatible training actions
are gray/inert. `/control` reads classifier-state telemetry instead of declaring
Standing/Fallen from the old pelvis predicate. Standalone critic-only work uses
the new environment but remains a separate diagnostic artifact; it does not
silently replace the paired critic.
identifies objective and paired critic architecture; incompatible training actions
are gray/inert. `/control` reads classifier-state telemetry instead of declaring
Standing/Fallen from the old pelvis predicate. Standalone critic-only work uses
the new environment but remains a separate diagnostic artifact; it does not
silently replace the paired critic.

### Validation and bounded smoke test

The full Python suite passed (178 tests), followed by 13 targeted tests after the
final best-policy telemetry changes. Tests cover reward/terminal semantics,
airborne start, settling, temporary imbalance, recovery counting, ordinary
truncation, no positive post-FALLEN rollout, finite encoded inputs and rewards,
separate gradients/optimizer updates, full paired checkpoint resume, legacy
rejection, and CLI training/evaluation workers. Short measurement fixtures test
policy updates; they are not claims of physically achievable recovery.

Debug classifier and behavioral termination tests pass. Release classifier,
behavioral termination, and the unchanged strict Release replay pass. Exact
historical endpoints remain **legacy 462 / Balance V2 516**, with all archived
numeric samples and the original physical digest preserved. CLI training,
evaluation and model-library screen tests pass, including balance readouts and
invalid telemetry handling. A real CLI PTY evaluation passes repeated evaluation,
aliases, resize, cancellation and unchanged-checkpoint checks. CLI, Python
extension, and Release viewer build successfully. The viewer build required
joining a pre-existing invalid macro line break in the ignored cached JSON
dependency header; no dependency version or project physics source was changed.
NumPy-unavailable and OpenGL-deprecation warnings remain nonfatal.

The bounded physical smoke test initializes a fresh seed-42 actor and seed-43
critic, collects one exploration episode (seed 130042), and performs one critic
update. It then evaluates the unchanged actor in both modes:

| Measurement | Collection | Deterministic | Stochastic (seed 10042) |
|---|---:|---:|---:|
| Episode steps | 373 | 441 | 433 |
| Balanced steps | 50 | 72 | 48 |
| Balanced duration (seconds) | 0.417 | 0.600 | 0.400 |
| Unbalanced steps | 322 | 368 | 384 |
| Longest balanced interval (steps) | 50 | 72 | 48 |
| Recoveries | 0 | 0 | 0 |
| Average physical quality | 0.21592 | 0.20517 | 0.18028 |
| Total reward | 80.53745 | 90.48005 | 78.06260 |
| Fall indicator | 1 | 1 | 1 |
| End reason | fallen | fallen | fallen |

Critic training MSE changed **0.10225897 to 0.06689423**; maximum critic parameter
change was 0.001000002. Actor change was exactly zero, actor optimizer updates
were zero, and actor gradients remained absent. A post-terminal step was rejected
without advancing simulation. Separate fixture tests prove that explicitly
requesting an actor update changes only actor parameters when critic updating is
disabled. No extended training ran and no smoke checkpoint was saved.

Detailed smoke measurements and the complete integration file inventory are in
`docs/traces/balance-system-integration/smoke.json` and `verification.json`.
All 186 pre-existing historical artifact files, including five checkpoints,
retain their original hashes. Changed files are grouped in the verification
manifest: environment/learning/metrics; explicit historical entry points; native
defaults/viewer telemetry; CLI workers/screens; regression tests; documentation.
No historical Release replay data or expectations were changed.

Remaining risks are intentionally measurable: small positive quality can reward
near-fall stalling, discounting may undervalue delayed recovery, geometric support
is not load-bearing proof, multiplicative quality can become almost flat, and
posture averaging can tolerate one badly oriented main part. The finite horizon
has no explicit time-remaining neural feature, so a state-only critic approximates
time-dependent values. No additional inputs were added. One critic update's
training MSE reduction is not held-out generalization, and the unchanged actor's
metrics demonstrate no learned recovery. The next research experiment can now
compare balanced duration and reward before/after short actor–critic training.

Reproduction of the bounded checks (use a fresh output report path):

```sh
PYTHONPATH=build-release:.:tests .venv/bin/python -m unittest discover -s tests -p 'test_*.py' -q
ctest --test-dir build --output-on-failure -R '^(termination_modes|balance_classifier)$'
ctest --test-dir build-release --output-on-failure -R '^(termination_modes|balance_classifier|termination_release_replay)$'
PYTHONPATH=build-release:. .venv/bin/python -m python.diagnostics.balance_system_smoke /tmp/aura-balance-system-smoke.json
```


## Historical configurable training runs and shared collision arena

This section records contract-5 behavior. New sessions use independent contract-6
workers described below; shared-arena evidence and replays remain historical.

The CLI now configures episodes per batch (2–128), consecutive batches (1–1000),
and simultaneous agents (1–4). New CLI runs default to 32 episodes, three batches, four
agents. Deterministic and stochastic evaluation follows each update; learning
rate remains 0.001. Historical Python experiment defaults remain unchanged. A batch collects complete episodes with frozen policy weights, then uses
the existing single actor and critic update. Eight episodes with four agents
means two waves of four. N repeats the configured block; P pauses and Q stops.
Each completed update is saved. Resume/restart restore the saved world and allow
new episode/batch counts; older solo checkpoints are not silently migrated.

`AuraArena` owns separate articulated bodies in one room. Each active body takes
its unchanged 120 Hz physics tick, optionally in parallel C++ tasks. Once all
finish, cross-agent physical shape contacts are resolved and each classifier is
advanced once. Parallel and serial stepping are bit-identical in the regression
fixture. The single-body stepping path and historical Release expectations remain
unchanged. Neural observations remain 205 and actions 32; architecture, exploration,
reward and optimizer scaling are unchanged.

Agents start on a 6 m grid in the existing 120 × 120 m room. No enlargement or
wall redesign is needed for four bodies. Inter-agent collision is enabled.
Existing contact shapes and local velocity impulses are reused; whole-agent
mass-weighted position translations preserve internal anchors and relative angles.
This is an experimental body-specific position correction, not a general coupled
articulated-contact solver. Room projection can reopen contact, and long crowded
rollouts still require validation. The per-body energy ledger does not account for
cross-agent impulse work. No arbitrary damping or motor changes were added.

Each agent has separate physical classification, reward history and terminal
metrics. A finished agent is removed from stepping and contacts until every agent
in the wave finishes, then the wave resets. No positive reward is collected after
FALLEN. Arena evaluation records individual metrics and displays their means,
rounding count fields for the existing CLI protocol. Initial landing is not recovery.
Viewer colors identify AURA 1 lavender, 2 green, 3 blue and 4 gold. Arena viewing uses a higher 1.05-radian pitch and 18.7 m camera distance, with the wireframe walls and ceiling hidden. The existing 120 × 120 m physical room and collisions remain unchanged; the floor and grid stay visible.

Shared-world episodes are coupled. Other agents are disturbances without explicit
other-agent perception in the fixed 205 inputs. The existing per-agent advantage
objective is retained; this is not a centralized multi-agent joint-return gradient.
Collisions are not evidence of better learning or recovery. This world has an
explicit contract (environment 4, reward 3, termination 2, collision version 1,
agent count, spacing, inactive-agent policy and evaluation aggregation). Solo
balance now uses environment 4. Incompatible world resumes fail explicitly.

A frozen seed-42 policy benchmark with four agents, exploration seed 555, and
three repetitions measured a median 3.332 s serial versus 1.350 s parallel,
about 2.47× faster for the same shared arena. Episode steps were identical:
400, 389, 426, 367. This is a local collection benchmark, not a guarantee of
end-to-end speedup or fourfold acceleration. Regression tests separately force
inter-agent contact; normally spaced neutral episodes need not collide.
Measurements and verification are stored in
`docs/traces/shared-arena-training/`. No extended training was launched and no
historical checkpoint was rewritten.

## Stationary-hold reward refinement (2026-10-15)

Balance-quality function version 2 adds `stationary_hold` and `foot_grip` factors.
While the classifier reports BALANCED, sustained horizontal COM speed above
0.08 m/s smoothly lowers `stationary_hold` (scale 0.30 m/s). Horizontal speed of
each geometrically grounded foot above 0.03 m/s smoothly lowers `foot_grip`
(scale 0.15 m/s). Airborne feet are excluded, and these two new penalties are
inactive during UNBALANCED recovery. The existing posture, height, support,
rotation, and general motion factors remain in place. Acceleration is not part
of the reward because the environment does not currently expose a measured COM
acceleration signal.

These initial deadbands allow small corrections; they are design tolerances,
not values calibrated from a standing-noise study. Geometric foot proximity is
only a proxy for load-bearing contact, and foot-center velocity can include
rolling motion, so evaluation should report the component values before further
tuning. A new quality-function version is included in the saved environment
contract. Older Balance System checkpoints therefore require a new model to use
this objective; their files remain available for historical evaluation.

## Floor friction model (2026-10-15)

Active `AuraSimulation` and the application fixed-step path now use contact-based
Coulomb friction. Tangential impulses are applied at actual floor contact points
and limited by the normal impulses at those contacts (initial coefficients 0.8
static and 0.6 dynamic). The old unconditional horizontal velocity damping
remains selectable as `legacy_velocity_damping` for historical replays; direct
floor-solver calls also retain that legacy default so existing isolated replays
do not silently change. Friction model version 2 is part of the Balance System
environment contract, so checkpoints trained with the prior contact response
must not be resumed as if they used this physics.

The coefficients are plausible starting assumptions, not measured material
properties. This is still a simplified per-rigid-body contact solver: it does not
solve the entire articulated character's ground contacts as one coupled system.
Validate stance drift and recovery over repeated episodes before treating the
new response as human-like or tuning the coefficients further.

## Ground contact version 3 and horizontal projection conservation

The default Balance System now uses environment contract **5** in both solo and
arena runs; reward 3 / quality 2 / termination 2 and the 205/32 neural interfaces
are unchanged. Ground version **3** records dry material coefficients (static 0.8,
sliding 0.6) and `mass_balanced_horizontal_com_v1` articulation projection. Earlier
checkpoint files are preserved; mismatched resumes report the changed fields.

A ten-second neutral Release reproduction demonstrated a physics-side source of
whole-body translation with motors enabled and with all motors disabled as an
explicit diagnostic comparison. Internal joint/self-collision projections moved
horizontal whole-body COM without corresponding horizontal momentum. The largest
single self-collision shift was about 0.15 m. The recorded viewer is the standalone
physics/control application, which reads `viewer_action.json`; that window alone
is not evidence of an actor-generated behavior.

The active application selects graph-component mass-balanced attachment repair,
then removes the horizontal COM change introduced by each complete internal joint
projection. The existing environment-feasible recenter translates the complete
body uniformly, retaining all relative poses and velocities and preserving floor
height. Self-collision projection receives the same horizontal COM conservation.
Room limits take precedence: a wall may restrict the recenter. This does not lock
the body's position or horizontal velocity; integrated movement and external
contact impulses remain free to move it. Legacy stepping retains its old
projection policy and archived numerical results.

The final room pass previously applied normal support without friction, after the
floor solver and joint passes. It could also use box corners for a part whose
floor contact uses a different shape. The active pass now uses the same floor
shape and contact solver as the floor pass, including load-limited friction and
foot-support diagnostics. Walls/roof retain their existing normal response. The
final floor call does not repeat angular damping: the existing floor-pass gain,
timestep, sweep order, iteration budgets and motor gains remain unchanged.

The active floor normal solver projects accumulated unilateral impulses, allowing
later contacts to retract excess earlier support instead of repeatedly adding
upward impulses at zero restitution. Tangential impulses solve the two horizontal
contact axes with an effective-mass matrix, then use the static/sliding friction
limits. The legacy add-only normal and velocity-damping response is retained for
historical replay. No global horizontal drag or velocity clamp was added.

`SimulationTrace` is an optional read-only callback at integration, floor, each
joint, self-collision and final-room boundaries. `aura_ground_motion_tests --report`
records COM, momentum, energy change, motor work and foot support. It uses raw
stepping past failure to inspect settling. The normal `/run` viewer also
continues after FALLEN; Space pauses/resumes, R restarts and P resets to the
paused initial pose. Python training, evaluation and model playback retain
episode termination and seal their terminal state. Rendering does not advance or correct physics.

Regression coverage includes two masses and axial/diagonal initial slip, a nine
second post-stop drift check, bounded friction impulses/energy, no airborne drag,
final-pass friction, consistent sphere-floor containment, per-stage horizontal
COM/momentum conservation, reset and continuous post-fall viewer stepping. The full-body gate
retains its existing anchor/overlap/floor/room thresholds. Historical Release
replay remains exactly 462/516, 643 archived samples, digest efa666e369d7f1e3.

Limits: this remains an iterative articulated-body approximation. A falling body
can genuinely translate, and raw post-FALLEN diagnostics with motors running can
still move. Floor/limit geometry can introduce vertical correction energy and
residual settling. This change does not establish autonomous stable balance,
perfect no-slip foot anchoring, or accurate arbitrary multi-contact dynamics.


## Independent workers and experiment integration — 2026-10-16

### Active contract and execution

New training uses `IndependentEnv`, not the coupled arena. A spawn-based process
pool receives a frozen CPU actor for the entire batch. Each logical worker owns
its own `AuraEnv` and simulation; self-collisions and ground contacts remain.
Only the parent owns learner optimizers. Completed episodes return states, raw
Normal samples, rewards, metrics, seeds and process identities. The parent
reconstructs differentiable log probabilities for the existing update algorithm.
Missing episodes, nonfinite results and worker exceptions abort the update.
Cancellation terminates and joins children. Workers use one PyTorch CPU thread.

A batch seed is `(training_seed + 1000000007 * update_index) mod (2^63-1)`.
Episode index adds `1000003 * index` modulo the same modulus. Assignment depends
on logical episode index, never process scheduling or worker count. Explicit
stochastic evaluation seeds bypass that derivation. Evaluation restores the
collector's update counter; it cannot consume the next training seed stream.

Default schedule: 32 episodes/update, three updates, min(4, detected CPU cores)
workers. Counts are separate; episodes need not divide by workers. Resource cap:
max(4, min(64, twice CPU capacity)), with a nonblocking warning above four. Workers
beyond the episode budget remain idle. Processes are created per collection call;
startup cost is included in reported throughput. Rollout inference is CPU. The
learner can use CPU, MPS or CUDA. GPU idle time during CPU physics is not failure.

Environment contract 6 adds `collection_mode=independent`, seed version 1 and an
explicit optional experimental-reward contract. Worker count is execution metadata,
not neural compatibility. Reward 3 / quality 3 / FALLEN termination 2, friction,
classifier thresholds and 205 observations / 32 actions are unchanged. Checkpoint
format stays 1 with additive design, critic, schedule, history and worker-change
metadata. Model-template and experiment schemas are 1. Critic return scale remains
100; changing an experiment's reward always starts a fresh critic and optimizers.

Compatible resumes preserve both networks, Adam state, RNG, history and seed
position while recreating workers. Historical coupled checkpoints remain evaluable;
Fork actor explicitly migrates compatible actor weights with fresh critic/Adam.
Architecture changes require fresh initialization, not partial tensor loading.
Experimental PPO checkpoints remain experimental and cannot silently resume using
production MC updates. Restart preserves saved network shapes and activation.

### Designs, experiments and evidence

`/create` opens the model builder directly. `/experiment` (`/experiments`) opens
only the experiment library. `/center` keeps separate saved Designs/Experiments
entries; these reuse rendering code without mixing their navigation tabs. Designs are configuration only; experiment checkpoints contain
actual initialized/trained weights. Supported hidden configurations are 0–8 layers,
1–2048 neurons each, Tanh/ReLU/Sigmoid/Identity, and default/orthogonal/Xavier
initialization. Actor output stays Tanh; critic output stays scalar Linear.

Registry kinds: frozen baseline, matched training, frozen-actor critic fitting,
physical reward-component audit, and worker throughput benchmark. Applicable
fields and comparison choices are validated by kind. Training reuses production
`train_batch` or the existing experimental clipped-PPO update. Reward formula
alternatives are named schema-1 product/geometric-mean/arithmetic-mean/minimum;
quality parameter changes are explicit. No arbitrary code/formula execution exists.
Every-step physical reward components are averaged, including diagnostics for the
terminal posture while the actual terminal reward remains zero.

Immutable experiment folders store configuration, frozen source snapshot and hash,
Python/build provenance, status/error evidence, per-update and evaluation records,
per-episode seed/worker assignments, output checkpoints and paired differences
against a matched control. Cancellation preserves completed evidence. A corrupt
source snapshot fails preflight. Completed runs are duplicated rather than rerun.
Paired deltas are descriptive, not significance claims. Evaluation is frozen:
one deterministic episode plus configured stochastic seeds, with dispersion,
balanced time/fraction, longest interval, recoveries, quality, return, survival,
fall rate and termination reason. Actual assigned training seeds are checked
against evaluation seeds, not merely their seed-list roots.

Promotion requires explicit review and all predeclared physical criteria across
enough seeds. It saves a default architecture with fresh weights, not trained
weights. Diagnostic limits, benchmark/reward-only studies, critic-only studies and
altered reward objectives cannot promote a production template. Merely finishing
does not validate a candidate. Frozen copies of one source actor do not count as
independent initialization seeds. Existing templates cannot be overwritten by name.

### Measured collection performance

One headless trial collected the same eight full episodes with an unchanged
64×64 actor; total 3,393 physics steps and all episode seeds/survival scores matched.

| Workers | Collection seconds | Episodes/s | Physics steps/s |
| --- | ---: | ---: | ---: |
| 1 | 13.597 | 0.588 | 249.5 |
| 2 | 7.293 | 1.097 | 465.2 |
| 4 | 4.242 | 1.886 | 799.8 |
| 6 | 4.821 | 1.660 | 703.9 |

Four workers were about 3.2× faster than one in this trial; six were slower than
four. This includes startup, is sensitive to system load, and measures collection,
not learning improvement. Raw results: `reports/independent-workers-20261016/benchmark.json`.
No extended training was launched.

### Files changed for this integration

New runtime modules:
`python/independent_env.py`, `python/worker_config.py`,
`python/model_templates.py`, `python/experiment_rewards.py`,
`python/experiment_library.py`, `cli/backend/experiment_ui.py`.

Existing runtime modules:
`python/aura_policy.py`, `python/train_nn.py`, `python/model_checkpoint.py`,
`python/arena_env.py`, `cli/backend/training_run.py`, `cli/backend/nn_session.py`,
`cli/backend/training_viewer.py`, `cli/backend/model_viewer.py`,
`cli/backend/critic_session.py`.

CLI/render binding:
`cli/app/Commands.hpp`, `cli/app/main.cpp`, `cli/terminal/Terminal.hpp`,
`cli/terminal/Terminal.cpp`, `cli/screens/ModelsScreen.cpp`,
`cli/screens/TrainingSetup.hpp`, `cli/screens/TrainingSetup.cpp`,
`cli/screens/NNTraining.hpp`, `cli/screens/NNTraining.cpp`,
`cli/screens/CriticScreen.hpp`, `cli/screens/CriticScreen.cpp`,
`cli/ui/TrainingCharts.hpp`, `src/python/AuraPython.cpp`.
The binding adds read-only worker-snapshot drawing; it does not step worker physics.

Verification/configuration/docs:
`tests/learning/test_independent_workers.py`,
`tests/learning/test_experiment_library.py`, `tests/cli/test_experiment_ui.py`,
`tests/cli/models_screen_tests.cpp`, `tests/cli/nn_training_screen_tests.cpp`,
`tests/cli/test_training_run.py`, `tests/learning/test_arena_training.py`,
`tests/learning/test_balance_system.py`, `tests/learning/test_model_checkpoint.py`,
`tests/learning/test_reward_repair.py`, `tests/learning/test_nn_configuration.py`,
`.gitignore`, `README.md`, `python/README.md`, `cli/README.md`,
`tests/README.md`, and this document. Historical collector tests explicitly select
historical collection; their physics and expected replay values are retained.
Earlier repository cleanup changes were already present and are not reset.

### Verification and limits

The final Python suite passes 254 tests. Eight CLI suites pass, including update
fingerprint detection, form/navigation behavior, disabled commands and compact
layouts. Spawned-worker checks cover identical sequential/parallel trajectories,
process identity, complete coverage, failure/cancellation cleanup, gradient
separation, worker-count resume and architecture-preserving restart. Experiment
checks cover bounded MC/PPO updates, frozen critic studies, source snapshots,
matched comparisons, persisted failures/cancellation, and promotion restrictions.

Debug portable termination passes. Release portable termination and exact
historical replay pass, retaining 462/516 endpoints and the archived digest.
The known preexisting ankle-attachment `joint` failure was not repaired or hidden;
the entire physics suite was not rerun for this UI/collection change.
All 282 historical model/evidence files in the preservation manifest match their
recorded hashes. Real CPU and MPS smoke tests each collected two complete episodes,
performed one paired update, then a frozen deterministic episode. Terminal reward
was zero and parameters/rewards remained finite; maximum actor parameter change
was about 0.001. These are execution checks, not proof of learning.

Computer-use inspection read the model center, experiment configuration and model
builder in CLion's existing terminal. It exposed crowded center shortcuts and the
initial builder's tuple/list display mismatch; both were corrected. Window resize
attempts were interrupted by concurrent user interaction, so compact native-window
visual verification is incomplete. Separate real pseudo-terminal smoke checks at
64×24 and 120×40 verified library states, builder navigation and cancellation;
frame tests check training charts and other screens at 60/100/160 columns and
20/40/60 rows. No claim is made that every screen/state was manually inspected.
Reopen `/experiment` to load fixes if a library instance was already open.

Only neutral starting posture is supported; no disturbance API was added. Recovery
time is not yet measured. There is no unrestricted architecture/plugin editor,
no automated hyperparameter search and no automatic promotion. Process pools are
not persistent, and large networks/workers can exhaust resources despite bounded
configuration. Architecture choices and reward alternatives are experimental,
not proven improvements. The single optional viewer renders sparse snapshots;
headless collection remains the performance measurement path.
