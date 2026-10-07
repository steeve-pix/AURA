# AGENTS.md — Project AURA

## Purpose and current scope

AURA (Autonomous Unified Reasoning Agent) is a learning-focused project for a
locally simulated embodied agent. Correctness, understandable architecture,
debuggability, and developer understanding matter more than producing code quickly.

The current repository implements a **C++20 3D humanoid body simulation** with
custom math and physics, articulated joints, and GLFW/OpenGL rendering. Active
work includes hierarchical joint corrections, floor contact, and body stability.
Python cognition and a brain/body bridge are future work; they are not implemented
here yet.

Use the actual code and the user's current task to determine scope. Do not impose
the former 2D grid-world or battery-seeking roadmap on this 3D implementation.
Do not assume that the full body is stable because individual joint tests pass.

## Repository map and responsibility boundaries

```text
include/aura/       Public headers, grouped by subsystem
src/math/           Vectors, quaternions, rotations, matrices
src/physics/        Rigid bodies, forces, integration, inertia, impulses, collision
src/body/           Humanoid parts, skeleton, joint geometry and constraints
src/render/         Window, camera, meshes, shaders, lighting, shadows
src/main.cpp        Application setup, input, simulation loop, rendering coordination
assets/shaders/     GLSL shader assets
tests/             Headless tests and full-body diagnostics
CMakeLists.txt      Libraries, application, dependencies, and test registration
```

Keep these responsibilities distinct:

- **Math** provides reusable numerical operations without body or rendering logic.
- **Physics** owns physical state and general physical calculations.
- **Body** owns humanoid structure and body-specific joint/subtree constraints.
- **Rendering** visualizes simulation state. Render transforms must not silently
  modify physical state or conceal broken joints.
- **Application** coordinates the systems. Prefer putting reusable behavior in
  the existing subsystem rather than expanding `main.cpp` indefinitely.

Preserve existing `aura::math`, `aura::physics`, `aura::body`, and `aura::render`
namespaces and the header/source organization. Do not reorganize the repository
or introduce a framework merely for stylistic consistency.

If a Python brain is introduced later, it should own high-level intentions,
goals, memory, and reasoning. C++ should retain physical execution and simulation.
Define the smallest useful documented interface when that integration is requested;
do not scaffold unused brain or bridge modules now.

## Working with the developer

Explain each new concept when it becomes relevant: what it does, why AURA needs
it, and where it belongs. Keep explanations tied to the current code.

- When the user asks to learn or implement something themselves, offer a small
  task and hints first, then review their attempt.
- When the user asks you to implement or fix something, carry out the scoped work
  and explain the important decisions. Do not turn an explicit implementation
  request into mandatory homework.
- Prefer one understandable change at a time: explain, implement, run, inspect,
  test, review.
- Avoid large code dumps, unrelated tutorials, and unexplained copy/paste solutions.
- Explain tradeoffs before a significant architectural change. Keep it as small
  as the demonstrated problem allows.

## Current engineering priorities

Unless the user requests another area, prioritize reliable foundations for the
articulated 3D body:

1. Correct coordinate transforms and rigid-body calculations.
2. Joint anchors, hinge axes, limits, motors, and velocity constraints.
3. Corrections that preserve connected descendant parts.
4. Interaction between floor contact and joint constraints.
5. Reproducible diagnostics and stability over multiple simulation steps.
6. Rendering that makes the physical behavior easy to inspect.

A useful body-stability milestone needs evidence: finite state, bounded joint
anchor gaps and limit errors, acceptable floor contact, and repeatable behavior
over a stated simulation duration. State the tested configuration and remaining
failures; do not claim general stability from a single pose or a short visual run.

Do not add cognition, LLMs, learning infrastructure, a game engine, or a replacement
physics engine unless requested or justified by a concrete limitation. Explain any
new dependency and its cost before introducing it.

## Physics and geometry rules

Read the relevant implementation and tests before changing numerical behavior.

- Explicitly distinguish local-space and world-space points, axes, and rotations.
- Existing hinge axes are expressed in part A's local frame. Joint angles and
  angular velocities use radians and radians per second.
- Preserve quaternion multiplication order and existing matrix conventions.
  Verify them in code; do not infer them from another library's conventions.
- Rotate a body around a world pivot by updating position and orientation together.
- When correcting a parent connection, account for its child subtree. Verify that
  descendant anchor gaps, relative angles, and relevant relative velocities are
  preserved. A local repair can otherwise break a neighboring joint.
- Treat position, orientation, linear velocity, and angular velocity corrections
  as separate operations with distinct effects.
- Account for floor contact when a correction moves connected parts. The current
  floor is at world Y=0; do not silently generalize a floor-specific technique into
  a universal collision solver.
- Preserve fixed-step simulation and inspect force/torque clearing and solver
  ordering. Changes to timestep, iteration count, sweep order, damping, or motor
  strength are behavior changes and require evidence.
- Do not hide instability by clamping state, disabling gravity or motors, moving
  the floor, relaxing test tolerances, or adding arbitrary damping. If a controlled
  experiment changes these settings, label it and compare with the baseline.

Experimental body-specific corrections may be appropriate while learning.
Document their assumptions and limits rather than presenting them as a general
physically accurate articulated-body solver.

`AuraSkeleton3D::collectComponent(body, start, jointToCut)` queries the fixed
humanoid topology without retaining body pointers. Cut joints must belong to that
skeleton; returned pointers belong to the supplied body. Head, neck, both shoulders,
both elbows, and both knees use graph-derived components for angular geometry.
Elbow/knee position and velocity corrections use the same graph components.
Their geometry wrappers change pose only and retain existing floor policies; the
generic `correctJointAngleWithComponent` does not decide floor feasibility.

## Debugging and review

Start with what happens, what was expected, and the smallest testable hypothesis.
Reproduce the issue before fixing it when practical.

For simulation failures, isolate the relevant joint or subtree first, then inspect
how it interacts with the complete body. Use measured anchor gaps, angle errors,
relative velocities, floor penetration, and the first failing step to narrow the
cause. Keep diagnostic output focused enough to interpret.

`tests/full_body_diagnostics.cpp` duplicates application setup and stepping for
headless investigation, with documented configuration differences. If changing
simulation ordering or setup, check both paths and keep intended differences
explicit. Do not extract a new simulation framework solely to remove duplication.
Use `--local-angular-limits` for the current application configuration (full humanoid
hierarchy, contact-aware component positions, local two-body anchor and angular-limit
impulses, hip/knee motors only). `--all-local-anchors` retains the earlier configuration
with legacy position and angular-limit handling. Add
`--legacy-geometry-compensation` when reproducing historical trajectories that
used geometric leaf velocity adjustments. Use `--left-limb-components` for the
previous subtree-velocity baseline; without either
flag the diagnostic retains the former pairwise left elbow/knee baseline.

`--leg-velocity-replay` copies the audited 3.725 s right-hip state and runs four
velocity-only right-leg sweeps. `ComponentVelocity` uses component COM momentum
and scalar axis inertia to audit common motion impulses. It is experimental and
is not used by the live solver; it excludes angular-limit velocity and contact
corrections. Energy-consistent scalar projections do not yet eliminate all actual
anchor-relative motion.

`--local-leg-velocity-replay` uses the same copied 3.725 s pose for eight
forward/backward right-leg sweeps with local two-body scalar impulses and
world-transformed inverse inertia. It audits work and kinetic energy without
descendant velocity propagation, geometry, contacts, motors, or angular limits.
This converges on the copied pose; that result alone does not establish live stability.

The mixed right-leg live experiment was reverted. `--right-leg-local-anchors`
retains that failed experiment for explicit comparisons (nonfinite at 2.116667 s
versus baseline 4.425 s). `--live-velocity-metrics` measures the old baseline.

`--skeleton-velocity-replay` copies the same finite baseline state at 3.725 s and
audits eight complete forward/reverse sweeps over all 15 joints using local
two-body anchor impulses only. Geometry, integration, gravity, motors, floor,
and angular-limit corrections are absent from the replay. It records the input
snapshot, per-joint speeds, kinetic energy, and each impulse's work/energy audit.
Its maximum anchor speed decreases from 31.279 to 0.1372 m/s, with decreasing
energy. This is isolated-pose evidence, not a full-body live stability result.

The application uses local two-body anchor and angular-limit impulses for all 15
joints, with 16 outer iterations and four inner velocity iterations. Angular-limit
impulses are equal and opposite in world space and change only endpoint angular
velocities. Legacy subtree anchor helpers
remain for baseline diagnostics. `--all-local-anchors` audits every anchor correction
and records maximum anchor speed after each existing forward/backward sweep (these
sweeps still include geometry). The 10-second run stays finite with no gap/limit
threshold failures and no detected anchor-energy injection, but reaches body Y=-1.73
and position distance about 62. This is not physically acceptable full-body stability.

`--floor-failure-diagnostic` runs the same all-local configuration unchanged,
records mass-weighted whole-body COM position/velocity at every completed step,
and saves all outer-iteration floor/forward/backward checkpoints for the first
completed-step penetration below -0.001. The first violation is the right hand
at 1.508333 s: its own floor solve restores Y=0, and subsequent geometry sweeps
reintroduce penetration. End-run COM radius about 60.77 explains most of the
61.98-unit body position radius as bulk motion, not attachment separation.

`--hand-geometry-momentum` traces each geometry operation at 1.508333 s, outer
iteration 16, including actual moved bodies and cut-component coverage. Forward
right-wrist pair translation and backward right-elbow subtree translation each
push the hand down about 0.007902 units. It also audits whole-body momentum by
stage/category without changing physics. The earliest completed-step horizontal
change >=0.1 kg m/s occurs at 0.366667 s in floor passes. Over ten seconds, local
anchor impulses conserve net momentum to numerical noise, while the retained
angular-limit propagation and geometry velocity compensation change net momentum
substantially. Do not attribute the eventual launch solely to that first contact.

`correctJointPositionWithComponents` prefers translating the child cut-component,
then the opposite component, provided no world-space lowest point acquires or
worsens floor penetration. Contact release upward is allowed. It modifies positions
only, uses a 1e-6 roundoff allowance at Y=0, and reports `MultipleContacts` without
moving either component when both candidates are blocked. This is a flat-floor
geometric policy, not a general collision solver.

`--component-position-replay` applies this policy to the copied 1.508333 s hand
checkpoint and verifies unchanged velocities. `--angular-momentum-diagnostic`
enables component positions but retains old angular-limit handling to capture its
first horizontal momentum change above 0.01 kg m/s: neck at 0.458333 s, with
delta Pz about -1.403594. `--local-angular-limits` enables the current local angular
impulses. Its ten-second run stays finite, has no gap/limit threshold failures,
minimum Y=-4.77e-7, and maximum position distance 13.33. Angular-limit operations
change net linear momentum by zero. Those measurements predate removal of
geometry velocity compensation; use `--legacy-geometry-compensation` to reproduce
that configuration. Do not claim general physical stability.

`--geometry-momentum-diagnostic` runs the current configuration unchanged and
records the first geometric operation whose horizontal momentum change exceeds
0.01 kg m/s. It emits the joint angle/limits/pivot and all 16 bodies' before/after
positions, orientations, velocities, and angular velocities. The first event is
the forward right-knee angle repair at 1.266667 s, iteration 1: the limb helper's
direct foot velocity compensation changes horizontal momentum by about 0.08730
kg m/s. This historical event is reproduced with `--legacy-geometry-compensation`.
The application no longer applies geometry velocity compensation.

`--knee-projection-replay` copies the 1.266667 s right-knee event (outer iteration
1, forward sweep). It compares legacy geometry with the same branch rotation and
floor lift without descendant velocity compensation, then records A/B/C/D and
64 frozen-pose full-skeleton forward/reverse local impulse sweeps. C and D each
contain one solve; each subsequent joint visit uses four inner anchor/limit solves.
It logs the input state and checks exact preservation of geometry versus the
legacy wrapper, exact A-to-B linear/angular velocities and linear momentum,
floor clearance, fixed replay poses, impulse energy/momentum roundoff, and a
1 mm/s all-anchor convergence target. This isolated replay does not alter the
application or establish live stability. It automatically enables the legacy
compensation on its snapshot-generating trajectory so the copied event remains
reproducible. Its projected copy has compensation disabled.


The application now projects elbow/knee geometry without descendant velocity
compensation. `--local-angular-limits --hand-geometry-momentum --live-velocity-metrics`
measures the current ten-second configuration: finite, no gap/limit failures,
minimum Y=-7.75e-7, max gap=9.56e-7, max position distance=8.30, peak KE=961.5 J.
The `geometryPoseProjection` and angular-limit momentum rows are exactly zero;
local anchor momentum changes are roundoff. Net horizontal momentum comes from
floor responses. Peak linear/angular speeds increased versus the compensated
baseline (27.46 m/s and 75.38 rad/s), so this is a conservation checkpoint rather
than general physical stability. Pose projections can still change rotational
energy through orientation changes at fixed world angular velocity.

`--floor-momentum-diagnostic` runs the current ten-second configuration unchanged
and retains the single floor operation with the largest whole-body horizontal
momentum change. `FloorCollisionAudit` optionally records actual contact points,
per-contact normal impulse sums, and separate damping/normal velocity changes
without altering the collision result. A copied replay checks exact live endpoint
velocity agreement and can account separately for historical right-shin-to-foot
response propagation. The former maximum was right shin at 8.283333 s, outer iteration 1:
horizontal delta magnitude 3.64678 kg m/s. Direct normal impulses add zero
horizontal momentum; propagating their angular response to the foot adds about
(-1.90545, -3.12650) kg m/s horizontally. This is not a Coulomb friction response.
Those measurements used the former nonmutating `Vec3::operator*=`. It is now
fixed to mutate and return a reference, so configured angular damping is active.
Do not expect regenerated trajectories to match the former inactive-damping run.


`tests/fixtures/right_shin_contact_8_283333.csv` stores all 16 body states from the
original inactive-angular-damping contact event. `aura_shin_contact_velocity_replay`
loads this fixture with skeleton anchors configured from the neutral pose before
loading body state. It preserves the shin-to-foot position translation but applies
floor velocity response to shin only, then runs frozen-pose local knee/ankle
anchor and angular-limit impulses. The fixture is registered as an isolated
velocity regression, not a full-body stability gate. Four sweeps reduce both
anchor speeds below 0.001 m/s; floor momentum accounting closes and foot velocity
is exactly untouched by contact. Live shin-to-foot velocity propagation has now
been removed; the geometric translation remains. The operator fix changes live
damping behavior without changing gains: the legacy `joint` four-part chain test
now fails its existing ankle-gap threshold. Do not weaken that threshold or tune
damping to hide this result. Two older collision-signature test build errors remain.


Runtime floor responses change velocities only on the contacting rigid body.
Right-shin floor position correction still translates shin + foot. The old motion
copying helper exists only in `tests/LegacyFloorResponse.hpp`; explicit
`--right-shin-floor-motion` enables it for comparisons. Other hierarchy flags now
select position propagation only. Current `--floor-momentum-diagnostic
--hand-geometry-momentum --live-velocity-metrics` checks all 307,200 floor
operations in ten seconds and finds exactly zero momentum change outside the
contacting body. Compared with the working-damping copied-motion baseline, max
horizontal floor event decreases 3.02035 -> 0.02145 kg m/s and position distance
9.49 -> 8.42. State remains finite with no gap/limit failures (min Y=-5.36e-7,
max gap=9.61e-7). Peak KE increases 737.7 -> 962.2 J and angular speed
63.20 -> 88.12 rad/s, so do not describe this as general physical stability.
Do not mix investigation of the separately failing legacy joint-chain regression
with this floor velocity change or undo the corrected angular damping.

`--floor-energy-diagnostic` runs the same current ten-second configuration and
records contacting-body KE before damping, after damping, and after the normal
phase, plus each individual normal impulse's point, magnitude, normal velocities,
KE and speed changes. The actual order is damping then normal impulses; measure
each stage against its own immediate input. Rotational KE uses the body-local
principal moments with angular velocity transformed into local space. Every floor
operation is replayed on a copy and checked for exact live endpoint velocity
agreement. This is read-only instrumentation and does not correct inverse inertia.
The unchanged run has 307,200 floor operations, 11,767 with contact and 14,856
normal impulses; 3,826 impulses add more than 1e-5 J, while damping adds none.
Worst normal impulse: right foot at 9.725 s, outer iteration 4, restitution zero,
J=1.90959, normal velocity -5.39979 -> about zero, KE +9.22938 J and angular
speed 22.87 -> 33.75 rad/s. Largest damping delta is negative (-3.51e-5 J).
The next physics target is the world/local inverse-inertia mismatch in the floor
effective mass and `applyImpulseAtPoint`; leave damping and gains unchanged.

`applyInverseInertiaWorld` in Inertia.hpp/.cpp now drives runtime floor effective
mass and `applyImpulseAtPoint`: transform world vectors into the normalized local
frame, divide by positive principal moments, and transform back. The retained
pairwise anchor solver uses the same helper in its denominator. MechanicalState
normalizes orientation when measuring local spin. The saved 9.725 s foot replay
retains an explicit historical response (+9.229378 J); the production impulse
response gives -7.140815 J. The ten-second floor-only baseline stays finite with
no gap/limit failures, max gap 9.90e-7, minimum Y -7.15e-7, peak KE 1072.3 J,
peak angular speed 55.60 rad/s. All complete normal phases stay below 1e-5 J
positive energy; two individual impulses reach about 1.21e-5 J (roundoff scale).
This replaces the prior coordinate-space bug; no gains/damping were tuned.

SelfCollision.hpp/.cpp is a position-only prototype using an inscribed sphere at
each physical COM (radius = half the smallest physical dimension). It does not
cover full boxes, limb lengths, or rendered capsules, and has no collision impulse
or friction. `shouldSelfCollide` requires the owning body as well as the skeleton
because adjacency is defined by member identity, not mutable names. Same-part and
all fifteen directly connected joint pairs are excluded; other pairs are eligible.
The skeleton shares its fixed connection table between graph traversal and the
adjacency query, without retaining pointers. Positive dimensions/masses are API
preconditions. Coincident sphere centers separate along world X.
The application currently leaves experimental self-collision disabled. The
headless diagnostic can apply one pair sweep after floor contacts and before
joint repairs in each existing outer iteration. It enables this
with `--self-collision`, which selects the current all-local velocity/component
geometry configuration (hip/knee motors only). Without this flag it keeps the
inertia-corrected baseline for comparison. `selfCollisionSummary` measures maximum
sphere penetration after completed steps in either configuration. Later joint
projections may reopen sphere overlaps; do not claim full-body nonintersection
from the isolated pair tests or add velocity copying to compensate.
The first ten-second self-collision run stays finite, with no joint gap/limit
threshold failures (max gap 1.53e-5, min Y -7.15e-7), but maximum completed-step
sphere penetration is 0.6737803 (baseline 0.6706313), position radius 128.7654,
and peak KE 10174.1 J.
It is an experimental pair-projection pass, not a stable full-body self-collision
solution: joint projections can undo separation and introduce large pose changes.
Do not add damping/clamps/iterations or claim volumes cannot intersect to hide
this failed interaction checkpoint. The newly corrected legacy joint regression
now passes without changing its threshold; physics/body_geometry tests still use
outdated floor signatures and fail to compile independently.
`--self-collision-window` uses the live sphere-projection configuration, buffers
only the final outer iteration, and stops at the first completed step with an
eligible pair penetration > 0.001. It prints before/after self-collision, every
forward/backward joint geometry operation, and end-step penetration/distance.
Moved-part identities and A/B full/partial component scopes come from observed
pose changes, not assumed solver choices. No physics/chooser policy is changed.
First event: 1.650 s, left thigh/right thigh. Penetration 0.014064252 before the
self pass -> 5.96e-8 after -> 0.007032096 after forward left-hip position repair
(moves left thigh/shin/foot) -> 0.014064133 after forward right-hip position repair
(moves right thigh/shin/foot). Hip angular repairs do nothing at this encounter;
backward repairs leave the overlap unchanged. Component selection currently
checks floor feasibility only. The alternative component's self-collision
feasibility was subsequently tested by `--self-translation-replay` (see below).

`componentTranslationIsSelfCollisionSafe` is a read-only sphere feasibility query
in physics; it checks eligible pairs crossing the supplied component boundary.
It allows unchanged/reduced existing overlap and rejects positive penetration
increases greater than the supplied tolerance. It is NOT wired into the live
position chooser. `--self-translation-replay` captures forward left-hip position
repair at 1.650 s, final iteration 16, evaluates both full translations on copies,
and stops after completing that unchanged live step. At tolerance 0.001 both
choices are floor-safe but self-unsafe: child hip gap 2.98e-8, opposite gap zero;
both thigh penetrations 0.007032096 and maximum positive overlap increase
0.007032037. Lowest moved Y: child 1.566652, opposite 0.2426735. Moving child by
-error versus the complementary component by +error produces poses differing
only by a global translation, so relative self-collision distances are identical.
Switching sides alone cannot fix this event. No fractional correction or runtime
geometry policy has been introduced by this copied-state experiment.

`--self-fraction-replay` captures that same pre-left-hip state. On copies only,
32 binary-search rounds find a floor-safe child translation using the crossing
pair no-worsening query with 0.001 per-operation tolerance. The measured fraction
is 0.14220719, remaining left-hip gap 0.006032060, thigh penetration 0.001000047
(increase 0.000999987), lowest moved Y 1.568960. The 16-cycle experiment applies
only the existing thigh-pair separation, then partial left/right hip component
translations; no integration, velocity, orientation, floor or other joint solves.
It does not converge: after separation both hip gaps return to ~0.007032, and
after both partial repairs they plateau at ~0.006032 with overlap ~0.002000034.
The overlap allowance is incremental per operation, so left and right repairs
each add ~0.001. Local fraction feasibility alone does not establish constraint
convergence. Live solver/chooser are unchanged; do not enable this policy or
increase cycles/tolerances to present it as a passing solution.

`--neutral-sphere-compatibility` checks the fresh assembled body before dynamics
and reports all 105 eligible pairs. None overlap. Neutral thighs: centers
(-0.34, 2.92, 0) and (+0.34, 2.92, 0), center distance 0.68, individual radii
0.29, required separation 0.58, clearance 0.10. The self_collision CTest checks
all 15 neutral joint anchor attachments and all eligible neutral sphere pairs.
`--hip-sphere-compatibility-replay` captures the 1.650 s pre-left-hip state and
forces both hip attachments on a copy using child-component translations only.
Orientations are fixed; no floor, self-collision or velocity responses occur.
Both hip gaps become 2.98e-8, but thigh distance is 0.5659358501 and penetration
0.01406413317. This proves incompatibility at those fixed dynamic orientations,
not in the initialized neutral body or in all possible joint orientations.
The application self-collision pass was removed pending a compatible dynamic
contact/joint model. Proxy dimensions, anatomy, filters and live joint geometry
policies remain unchanged; experimental headless self-collision remains available.

SelfCollision now also provides detection-only CollisionCapsule,
bodyPartCollisionCapsule, closestPointsBetweenSegments, and capsulePenetration.
Limb radius is half min(size.x,size.z), straight half-length max(0,size.y/2-radius).
Endpoints follow local Y transformed by normalized orientation and world position.
This uses physical dimensions, not rendering insets; short capsules collapse to
spheres, so this is not a general fitting rule for flat feet/other body parts.
Closest segment points include fractions clamped to [0,1]; double coefficients
and a cross-product determinant handle parallel and near-parallel cases, including
zero-length segments. Capsule penetration is max(0,radii sum - segment distance).
There is no capsule resolution, impulse, runtime filtering or chooser integration.
The existing neutral/attached-hip compatibility modes now emit hipCapsuleGeometry
and hipCapsuleBody alongside sphere measurements. Neutral thighs have segment
distance 0.68, radii sum 0.58, zero capsule overlap. The saved attached 1.650 s pose
has interior closest fractions ~0.288372 and 0.761558, segment distance 0.05027333,
and penetration 0.52972662. Center spheres underrepresented the collision, rather
than creating a false positive: each limb capsule contains its same-radius center
sphere. This is a fixed-orientation configuration conflict, not a neutral-body
proportion failure or evidence that all orientations are impossible.
The capsule_collision CTest covers transforms, parallel/crossing/skew/endpoint
and collapsed segments, touching and separated capsules, neutral thighs, and the
saved pose tests/fixtures/attached_thigh_capsules_1_650.csv. The closest connector
in that fixture is perpendicular to both interior segment axes. Runtime
self-collision remains disabled. Do not add capsule response until requested.

`--hip-capsule-rotation-replay` captures the same 1.650 s final-iteration pose,
attaches both hips on a copy at fixed orientations, and emits all 16 pose/velocity
inputs. tests/fixtures/thigh_hip_rotation_1_650.csv saves this state. The source
already has ~0.007032 knee gaps introduced by the preceding sphere pass; do not
misreport them as rotation-induced or assume this snapshot is fully attached.
The test-only HipCapsuleRotationReplay.hpp searches world-space axes from
(closest point - fixed hip pivot) cross separation direction. Opposite separation
forces are already encoded in the two axes; a second right-angle sign inversion
is not needed. Each candidate uses signed 0, 0.005, 0.01, or 0.02 rad steps on
both sides, preserving each leg component rigidly around its pelvis hip pivot.
Candidates must reduce capsule penetration, keep all six leg bodies out of the
floor (1e-6 roundoff), preserve baseline attachment gaps (1e-5 roundoff), and
satisfy existing hinge-twist limits (1e-5 rad). The axes/step signs are recomputed
from the current copied contact at each accepted step; the search budget is 128
steps, not a change to runtime solver iterations. Coincident closest points stop
with a diagnostic rather than inventing an arbitrary collision normal.
Source copy: 23 steps remove 0.5297266 penetration, preserving pre-existing knee
gaps and velocities. A separately labelled reference translates shin+foot during
setup to align each knee without changing thigh geometry/orientation, then uses
rotations only: 24 steps remove the same overlap, all six gaps remain below
1.824e-6, descendant hinge-angle drift below 1.04e-6, and feet remain above floor.
The search is greedy and addresses this thigh pair only; other self-collision
pairs, room walls, global energy, strict hinge swing constraints and live dynamics
are not solved. Current joint limits constrain twist, so compliance here is not
a claim of full anatomical 3D angular validity. No velocities/impulses change.
The hip_capsule_rotation CTest uses the saved full-body input and checks monotonic
accepted penetration, gaps, descendant full relative orientations, floor/limits,
fixed pelvis, and untouched velocities for both input configurations. Runtime
self-collision remains OFF; the live chooser/solver were not changed.



In reviews, identify what works and distinguish **must fix**, **should improve**,
and **optional** findings. Explain their observable consequences. Preserve the
developer's code where practical; avoid rewrites based only on style preferences.

The application now has a static room matching the original 120 x 120 platform:
X/Z in [-60,60], floor Y=0, ceiling Y=30. `RoomCollision.hpp/.cpp` uses world-space
box corners and inward plane normals. After each existing forward/backward joint
sweep pair, it translates the whole body by one common displacement into the
room, preserving joint geometry, then applies four normal-impulse passes only to
contacting parts. It adds no damping, friction, restitution gain, or geometric
velocity compensation. The existing floor friction remains. Room contact energy
has its own runtime diagnostic category. `resolveRoomCollisions` reports false
without modifying a group too large to fit; ordinary humanoid configurations fit.
The walls/roof render as wire grids to retain visibility; collisions use physical
oriented boxes, not rendered capsule/ornament surfaces.
`--room-bounds` selects the current self-collision/local impulse/component geometry
configuration plus this same final room pass. Without it, the headless trajectory
retains its previous room-free configuration. The ten-second room run remains
finite with zero completed-step physical-box room penetration, zero group-fit
failures, max anchor gap 3.844384e-6, and no gap/limit threshold failures. It still
has self-overlap (maximum sphere penetration 0.733533) and peak joint-relative
angular speed about 208.79 rad/s. This establishes containment for the measured
run, not a fix for unstable motion or physically acceptable body stability.


## Build and validation

CMake requires version 3.20 or later and C++20. The project currently fetches GLFW
3.4 through FetchContent and requires OpenGL. Initial configuration may need network
access for GLFW; running the graphical app needs a usable display/OpenGL context.

For a fresh build from the repository root:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Reuse a suitable existing build directory when available. Check its configuration
rather than overwriting the developer's IDE settings. Multi-configuration generators
may require `--config Debug` for the build and `-C Debug` for CTest.

Run the application or headless diagnostic explicitly as needed:

```sh
# macOS (Finder launch):
open build/AURA.app
# macOS (Terminal launch, with diagnostics visible):
./build/AURA.app/Contents/MacOS/AURA
# Other platforms:
./build/aura
# Headless diagnostic on all platforms:
./build/aura_full_body_diagnostics
```

On macOS, target `aura` builds `AURA.app` using the configured architecture
(`arm64` on the current Apple Silicon build). CMake bundles the `assets/` tree
under `Contents/Resources/assets`; `src/AssetPath.cpp` resolves it through the
main bundle, independent of the working directory or checkout location. Rebuild
with `cmake --build build --target aura --parallel` after code or asset changes.
Finder launches do not display terminal diagnostics. Other platforms retain the
plain executable and source asset directory. This is a local development bundle;
distribution signing/notarization is not configured.

- Tests use small standalone executables registered with CTest. Follow the existing
  style instead of adding a test framework without a demonstrated need.
- Add or update observable-behavior tests for physics, geometry, or constraint
  changes. Cover relevant boundary cases and use justified floating-point tolerances.
- Run focused tests while iterating and the registered suite for a completed code
  change. For solver changes, also inspect the relevant full-body diagnostic.
- The full-body diagnostic is deliberately not a CTest gate: its current exit code
  reports whether state stays finite. Exit code zero does **not** mean all joint
  gap, angle, or stability thresholds passed. Read its measurements.
- For rendering changes, perform visual inspection when possible. Headless math
  tests cannot establish that a shader or rendered image is correct.
- Documentation-only changes need a content/diff check; a full rebuild is unnecessary.
- Report which checks ran, their results, and any checks that could not run.

## Scope, working tree, and Git

Inspect `git status`, the current branch, relevant files, and existing changes before
editing. Explain the scope briefly before changing several files.

- Preserve uncommitted and untracked work. Do not discard, overwrite, stage, or
  commit unrelated developer changes.
- Use `feature/*` for features and `fix/*` for fixes. New work normally branches
  from `dev`; do not develop features directly on `main` or `dev`.
- Continue on an appropriate existing task branch. With a dirty working tree, do
  not switch branches or pull automatically just to enforce the normal workflow.
- `dev` is the integration branch; `main` is for stable releases. Use `release/*`
  when preparing a real milestone, with tested releases tagged appropriately.
- Keep commits small and use descriptive Conventional Commit messages such as
  `fix: preserve descendant anchors during shoulder correction`.
- Commit, merge, push, tag, or delete branches when requested as part of the task;
  completing an edit alone is not a request to publish or integrate it.
- Explain relevant Git concepts as needed. Inspect merge conflicts before resolving
  them; do not blindly replace conflicted files.
- Avoid destructive Git commands and force pushes. Explain potential loss and safer
  alternatives, and obtain explicit authorization before destructive actions.

Keep changes scoped to the requested step. Update these instructions when the
architecture or working practices actually change, so future agents can rely on
them without following an obsolete roadmap.
