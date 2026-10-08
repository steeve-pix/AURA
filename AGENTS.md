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
Use `--hip-swing-limits` for the current application configuration (bounded Y/Z
hip swing projection plus the room pass, self-collision off). Use
`--local-angular-limits` for the earlier twist-only configuration (full humanoid
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


### Opt-in thigh capsule live experiment

`--thigh-self-collision` enables only left-thigh/right-thigh capsule pose
projection in the application and headless diagnostic. `ThighSelfCollision`
belongs to body articulation: one candidate search per existing outer iteration,
after forward/backward joints and before room containment. Signed half/full
steps never exceed 0.01 rad per leg per call. Torque-derived axes rotate whole
leg cut-components around fixed hip pivots. Candidates must reduce penetration,
preserve the six leg anchor gaps within 1e-5, satisfy existing twist limits
within 1e-5, and keep all six physical leg boxes above floor within 1e-6.
Velocities are untouched; these are the current twist limits, not full 3D hip
anatomical constraints. Coincident closest points have no fallback normal.

For matching room-inclusive OFF/ON runs use `--room-bounds
--no-sphere-self-collision --live-velocity-metrics`, adding
`--thigh-self-collision` only for ON. The explicit no-sphere flag overrides the
historical sphere-pass implication of room-bounds regardless of argument order.
No settings, gains, timestep or iteration counts change. Ten-second results:
OFF/ON max completed-step thigh penetration 0.579609/0.2515273, max leg gap
9.54e-7/7.15e-7, minimum foot Y zero in both. ON has 358 accepted corrections,
675 blocked overlapping calls, maximum per-leg step 0.01 rad. Both stay finite
without gap/limit failures or room penetration. Peak KE rises 801.6 -> 1045.6 J;
this is not a successful nonintersection or general stability milestone.
The experiment remains opt-in; generic sphere/capsule resolution is not enabled.
Application logs cumulative completed-step thigh/leg/foot metrics every120 steps
and resets them with restart/pose. Headless `thighCollisionSummary` reports the
same metrics plus blocked calls. Artifacts: build/thigh_live_off_10s.csv and
build/thigh_live_on_10s.csv. The hip rotation test also checks the production
bounded helper, unchanged velocities, neutral no-op and blocked-floor behavior.

### Thigh collision rejection audit

`ThighCollisionAudit` is optional read-only output from the bounded thigh helper.
It records all 24 signed half/full-angle candidate pairs, including candidates
which do not improve penetration. Floor flags distinguish each foot from the
other leg bodies; limit errors include all six leg joints; anchor drift is separate.
A blocked-call reason is counted when at least one improving candidate fails that
check. Counts overlap and are not an exclusive partition. `floorOnly` and
`limitsOnly` partition calls whose improving-candidate rejection union contains
only that constraint family. The diagnostic also records whether every improving
candidate fails floor or limits, and saves the worst completed-step pose and the
worst blocked-call trials separately. No new-pair collision feasibility check exists;
it is explicitly reported as not implemented rather than claiming zero rejections.

The same room-inclusive ten-second ON trajectory still has 358 accepted and 675
blocked calls. All 675 have an improving candidate rejected by a floor check;
488 also have improving candidates rejected by joint limits. 187 are floor-only,
none limit-only. Individual overlapping counts: left foot 517, right foot 126,
right thigh/shin 47, left thigh/shin 0, left limits 423, right limits 212. No call is
blocked by lack of improvement, degeneracy, or anchor drift. Instrumentation
reproduces all 38,400 velocity-sweep lines and run/energy/room summaries exactly.
The worst completed-step overlap occurs at 9.975 s, penetration 0.2515273,
left/right hip angles 0.7999855/0.1790255 and feet Y 2.062015/0.6176354. It still
has a feasible right-leg-only  +0.01 rad candidate, reducing penetration to 0.2397777;
do not confuse this finite per-iteration progress with the blocked-call cases.
`build/thigh_block_reasons_final_10s.csv` contains per-call masks, counters and
both worst-case candidate tables. Audit/no-audit endpoint equivalence and candidate
classification are covered by the hip capsule rotation test. No live feasibility
policy, angle cap, damping, gains, timestep, or iteration counts were changed.
The final audit distinguishes pre-existing floor violations from candidate-induced
ones: 45 blocked calls already have a floor-invalid leg; 32 have an improving
candidate passing a diagnostic no-worsening floor test with the same limit/gap
checks. That hypothetical policy is NOT enabled. Every improving candidate is
floor-rejected in 322 calls and limit-rejected in 236 (these counts can overlap).
The worst blocked call is at 1.508333 s, iteration 1, overlap 0.1424013, hips
0.7989424/0.7999999, feet Y -0.02749506/0.4039449. A right-only +0.01 rad trial
reduces overlap to 0.1396259 and satisfies limits, but is rejected because the
unchanged left foot already penetrates. A left-only +0.01 rad trial reduces
penetration to 0.1341601 but worsens left-foot penetration and exceeds the left
hip limit by 0.0017465 rad. Thus existing-floor rejection is a real policy
restriction, but accounts for a minority of calls; the majority start floor-valid.
The 0.01 cap, runtime solver decisions and trajectory remain unchanged. The final
artifact matches the original ON run's 38,400 velocity-sweep lines and all
run/energy/thigh/room summaries exactly. All 29 runnable tests pass; the two
pre-existing outdated floor-signature tests remain excluded.

### No-worsening floor policy for thigh candidates

The opt-in thigh correction now uses `ThighFloorPolicy::NoWorsening` by default.
Only its floor test changed: each physical leg body's candidate lowest Y is
compared with that same body's input Y. Input Y >= -1e-6 requires output Y >=
-1e-6; deeper existing penetration requires output Y >= input Y -1e-6. The
helper does not resolve unrelated existing penetration, and all twist-limit,
anchor-gap, axis, angle-cap, ordering, velocity and room policies remain intact.
`--strict-thigh-floor` retains the previous absolute-clearance check for explicit
headless comparisons; it does not turn on the thigh experiment by itself.

`--thigh-floor-policy-replay` generates the exact previous strict-policy
trajectory with room bounds and no sphere pass, stops before the 1.508333 s,
iteration-1 correction, and evaluates both policies on copies. Old rejects; new
accepts right +0.01 rad, overlap 0.1424013376 -> 0.1396258771. Left foot Y is
exactly unchanged (-0.02749505639), right foot Y remains positive
(0.4039449394 -> 0.4014346302). All 16 bodies' velocities remain exactly unchanged,
all six leg gaps are preserved within 1e-5, no floor violation worsens, and limit
error is zero. The replay checks those invariants and returns failure if any fail.
Artifact: build/thigh_floor_policy_replay.csv. Unit tests cover clear, near-clear,
unchanged/decreased existing penetration, worsening rejection and the strict
comparison behavior on an overlapping fixture with pre-existing penetration.

Matching ten-second live runs use `--room-bounds --no-sphere-self-collision
--live-velocity-metrics --thigh-self-collision`, adding `--strict-thigh-floor` only
for the old policy. Artifacts: build/thigh_floor_strict_10s.csv and
build/thigh_floor_no_worse_10s.csv. Strict reproduces all 38,400 previous velocity
sweep lines and run/energy/room summaries exactly. Strict -> no-worsening:
accepted 358 -> 1696, blocked 675 -> 1705, floor-only blocked 187 -> 421,
floor+limits blocked 488 -> 923, limits-only blocked 0 -> 361, maximum completed
thigh penetration 0.2515273 -> 0.5394549. Both runs stay finite without joint
gap/limit threshold failures, minimum completed foot Y zero, and zero room
penetration. Maximum leg gap 7.15e-7 -> 9.61e-7; max angle remains 0.01 rad.
New-policy blocked calls with an improving candidate passing no-worsening floor
and the same other checks: zero (old 32). Peak KE 1045.6 -> 904.2 J.

This fixes the demonstrated local false floor rejection but does NOT improve live
thigh separation. The trajectory changes, encounters more overlaps, and the worst
completed overlap is now at 5.675 s with left hip/knee and right ankle at their
limits and the right foot on floor. Rejection reason counts still overlap; do
not infer global geometric impossibility or label joint limits the sole blocker.
No knee/pelvis participation or stronger rotations were added. The collision pass
remains opt-in and is not a successful full-body nonintersection solver. All 29
runnable tests pass; the two unrelated outdated floor-signature tests remain
excluded.

### Copied hip + knee floor compensation

`--thigh-knee-replay` runs the unchanged current no-worsening, room-inclusive,
hip-only live trajectory without the sphere pass. It classifies improving
candidates in blocked calls as A (floor-rejected, both hip limits satisfied) or
B (at least one hip limit violated, regardless of floor). Calls can contain both
candidate classes. Ten-second counts: A calls 1092, B calls 1284, both 671;
exclusive partition A-only 421, B-only 613, both 671 = 1705. Improving candidate
counts A 7776, B 11574; 6386 B candidates have no floor rejection.

The copied experiment selects a one-sided hip trial with only that leg's foot
floor flag, all limits satisfied, no anchor drift, and all six input leg boxes
floor-valid within 1e-6. `tests/ThighKneeReplay.hpp` reproduces the same hip axis
and pivot, then searches signed knee steps in 0.0005-rad increments, smallest
magnitude first, up to 0.01 rad. The knee world axis is transformed from its local
hinge using the already hip-rotated thigh, and its pivot is that thigh's knee
anchor. Only shin + foot rotate. Every candidate checks all six leg boxes against
input no-worsening floor feasibility, all leg twist limits and anchor preservation.
No impulses, integration, motors, floor/room repair, or live knee participation.

The FIRST eligible replay succeeds: 1.400000 s, outer iteration 2, left hip
+0.005 rad. Initial thigh penetration 0.0011601448 becomes zero, but hip alone
lowers left foot from -1.192e-7 (roundoff) to -0.0017089844. Knee +0.0015 rad
(the sixth tested signed trial) restores foot Y to +0.0005810559, retaining zero
overlap and the exact same thigh/hip state. Hip angle 0.79955685 is unchanged by
the knee; knee angle 1.37785995 -> 1.37935996. Hip/knee/ankle gaps after coupling
2.46e-7 / 2.73e-7 / 2.38e-7, zero limit error, all 16 velocities exactly unchanged.
This proves knee compensation can rescue this floor-blocked hip trial, not that
it can repair a hip-limit violation or stabilize live self-collision.

`tests/fixtures/thigh_knee_floor_block.csv` saves all 16 input bodies and the hip
choice. Configure skeleton anchors from neutral before loading it. The
`thigh_knee_replay` CTest checks the actual hip-floor failure, coupled feasibility,
unchanged thigh geometry, all leg attachments/limits/floor depths/velocities and
full ankle relative orientation. Artifact build/thigh_knee_replay_10s.csv records
the input, comparison table and classification. All 38,400 velocity-sweep lines
and run/energy/thigh/room summaries match build/thigh_floor_no_worse_10s.csv
exactly. All 30 runnable tests pass; the two unrelated outdated floor-signature
tests remain excluded. The application physics and default collision mode are
unchanged; no multi-joint resolver has been wired live.

### Opt-in live Class-A knee compensation

`--thigh-knee-compensation` enables the thigh pass plus knee compensation in the
application/diagnostic; `--thigh-self-collision` alone retains hip-only behavior.
Neither is on by default. Production `correctThighCapsuleOverlap` has a final
`compensateKnees` option defaulting false. Only improving hip candidates whose
sole rejections are moved legs' FOOT floor flags enter compensation. Any hip,
knee or ankle limit error, anchor drift, or thigh/shin floor rejection excludes
that candidate. Class-B hip-limit candidates never try knee motion.

The bounded search uses the already hip-rotated thigh's world hinge axis and knee
anchor, rotates shin + foot only, and probes +/-0.001 rad. Directions must raise
the lowest foot point. In each such direction it samples successive 0.001-rad
magnitudes up to 0.01, retains the first fully feasible bracket, and bisects it
12 times while retaining a checked feasible endpoint. It chooses the smaller
found magnitude across directions. This is refinement within the first sampled
feasible bracket, not proof of global optimality for nonmonotonic motion. All
leg limits/gaps and per-body no-worsening floor checks are verified again after
both legs' compensation, with exactly unchanged thigh penetration. Nothing
copies/invents velocities; no pelvis/other-joint correction is added. Audit
records raw hip rejections and knee attempt/feasibility separately.

The saved Class-A fixture exercises the production path: selected hips
+0.005/-0.005 rad, left knee about +0.00111914 rad, right knee zero. The test
checks no compensation on Class B, pose-only state, all attachments/limits/floor
feasibility, exact audit/no-audit endpoint agreement, and loss of floor feasibility
when the selected knee magnitude is reduced by 1e-6 rad.

Matching ten-second room-inclusive runs use `--room-bounds
--no-sphere-self-collision --live-velocity-metrics --thigh-self-collision`, adding
`--thigh-knee-compensation` only for ON. Artifacts:
build/live_class_a_hip_only_10s.csv and build/live_class_a_hip_knee_10s.csv.
OFF -> ON: hip-only accepted 1696 -> 909, hip+knee accepted 0 -> 42;
blocked total 1705 -> 586; calls with A candidates 1092 -> 110, with B candidates
1284 -> 586, both 671 -> 110. Exclusive A-only 421 -> 0, B-only 613 -> 476.
All remaining calls contain B, but 110 also contain unsolved A candidates; do
not present hip limits as the sole remaining constraint. Max thigh penetration
0.5394549 -> 0.5462599 (slightly worse), minimum completed foot/body Y zero in
both, max leg gap 9.61e-7 -> 1.43e-6. Both finite, no gap/limit threshold failures,
zero room penetration or group-fit failures. Largest knee compensation 0.00969751
rad, hip cap unchanged 0.01. Peak KE 904.2 -> 926.9 J; no suspicious anchor
energy impulses. This validates live compensation but not full self-nonintersection.

The OFF trajectory's 38,400 sweep lines and run/energy/room summaries exactly
reproduce the previous no-worsening baseline. All 30 runnable tests pass; two
unrelated outdated floor-signature tests remain excluded. The mode remains
experimental and opt-in; gains, damping, timestep and solver ordering are unchanged.

### Copied shared-hip Class-B search

`--shared-hip-replay` generates the current room-inclusive hip+knee opt-in
trajectory with sphere projection off, stops at the first blocked call containing
Class B but no Class A, and searches copies only. The event is 1.433333 s,
outer iteration 1. Both hip angles are at their +0.8 rad maximum; thigh capsule
penetration is 0.00830555. The intermediate left foot is already at -0.01988094,
so candidate floor feasibility uses per-body no-worsening, not absolute clearance.

`tests/SharedHipReplay.hpp` tests 6,561 signed rotation pairs at 0.00025 rad
spacing within the unchanged +/-0.01 rad per-hip cap, including single-side,
symmetric and biased pairs. Torque-derived axes already encode opposite
separating forces: +0.005/+0.005 is the symmetric separating trial, while
+0.005/-0.005 is also reported separately. Rotations move complete leg components
around their hip pivots. Knees/pelvis receive no independent adjustment; all six
leg limits, anchor-gap drift and floor no-worsening are checked, and velocities
remain exactly unchanged.

Left-only +0.005 reduces penetration to 0.00484389 but exceeds the left hip
limit and worsens left-foot depth. Right-only +0.005 gives 0.00687593 and exceeds
the right hip limit. Symmetric +0.005/+0.005 gives 0.00341541 but violates both
hip limits and the left floor check. Of the grid trials, 1,681 are feasible and
none improves penetration by more than 1e-7. Ignoring feasibility finds zero
penetration at +0.008/+0.00975, but that violates both limits and floor policy.
This is failure to find a feasible correction along these two fixed axes and
sampled budget, not proof that every possible 3D hip motion is impossible.

Artifact build/shared_hip_replay.csv and fixture
tests/fixtures/shared_hip_class_b.csv retain the original candidates and all 16
body states. The shared_hip_replay test checks the saturated limits, separation
versus feasibility conflict, preserved leg anchors and unchanged velocities.
The live helper already tries coarse paired hip rotations; this diagnostic
refines distribution rather than introducing paired rotations for the first
time. Runtime physics, defaults and Class-A knee logic are unchanged.
All 31 runnable CTests pass; the two pre-existing outdated floor-signature
tests remain excluded. No additional ten-second live experiment is needed for
this copied-only diagnostic, and no shared-hip runtime policy was enabled.

### Copied secondary hip Z swing

`aura_hip_z_replay_tests tests/fixtures/shared_hip_class_b.csv` reuses the exact
1.433333 s Class-B snapshot. `tests/HipZReplay.hpp` first applies pure rotations
around pelvis-local Z transformed to world, at fixed world hip pivots. Outward
signs in this tilted pose are left negative/right positive. Raw -0.01/+0.01
reduces penetration 0.00830555 -> 0.00124580 but changes the measured X twists
to 0.7965945/0.8083278 and worsens left-foot Y -0.01988094 -> -0.02501024.
Thus pure parent-local Z does not preserve extracted X twist in this pose.

The second copied variant removes the candidate's quaternion X twist and
reattaches the original X twist (`candidateSwing * originalTwist`). This is a
Z-driven, twist-preserving swing projection, not a pure Z-only rotation. Its
world rotation updates each entire leg's orientation and position around the
hip pivot. No knee/pelvis adjustment, velocity change or runtime solver wiring
is introduced. Constrained -0.01/+0.01 lowers overlap to 0.00333887, keeps both
X twists at 0.8, but worsens the existing left-foot depth to -0.02103437.

A copied search samples unequal outward shares at 0.05 ratio spacing, brackets
with 0.001-rad Z probes up to 0.25 rad per hip and refines the first feasible
zero-penetration endpoint with 20 binary rounds. Among these sampled shares,
the smallest maximum probe is left zero/right +0.160193 rad (about 9.18 degrees).
Penetration becomes zero; X twists are 0.8000000/0.8000002; left foot depth stays
-0.01988094 and right foot is +0.2032191; maximum leg anchor gap is 6.08e-7.
All leg limits/no-worsening floor checks pass and all sixteen bodies' velocities
are exactly unchanged. The input is an imperfect intermediate pose; this is
no-worsening floor feasibility, not absolute clearance of the left foot. The
search does not prove a global minimum over all swing directions or ratios.

Artifact build/hip_z_replay.csv records raw and constrained trials. CTest
hip_z_replay verifies the raw twist coupling, the constrained floor conflict,
successful biased separation, all leg anchors/limits, unchanged upper-body
poses/velocities and preserved full knee/ankle relative orientations. All 32
runnable tests pass; the two existing outdated floor-signature tests remain
excluded. Runtime joint types, axes, gains and physics are unchanged.

The existing Joint3D implementation measures, limits and motors only twist
about hingeAxis; it does not enforce transverse swing locking for a true hinge,
and minAngles/maxAngles are not active multi-axis limits. This experiment
validates useful secondary swing geometry, not a completed ball-joint model or
proof that the live solver formerly locked every non-X rotation.

### Initial reusable SwingTwist representation

Joint3D now appends `JointType { Hinge, SwingTwist }` and Z swing bounds without
breaking existing aggregate initializers. Both hips advertise SwingTwist with
minSwingZ=-0.4/maxSwingZ=+0.4 rad; knees/elbows and other joints retain Hinge.
These are initial experimental bounds, not calibrated anatomical ranges.
The live solver still handles axial twist exactly as before: type metadata does
not yet activate general swing-limit geometry/velocity constraints or transverse
locking on Hinge joints. Self-collision remains OFF by default.

JointGeometry now exposes relativeJointSwingTwist, relativeJointTwistAngle,
relativeJointSwing and relativeJointSwingZ. Relative orientation in part A's
local frame is `swing * twist`; the twist axis is joint.hingeAxis. Swing Z is the
Z component of the shortest quaternion rotation vector (`log(swing)`), not an
Euler angle and not the probe angle. Y swing remains unbounded in this narrow
first representation. At perpendicular 180-degree swing, twistDefined=false;
the decomposition uses identity twist while reconstructing orientation, and
the legacy scalar angle still returns zero. relativeJointAngle delegates to
the named twist helper while retaining the previous angle calculation.

`applyJointSwingWithComponent` supports X-axis SwingTwist joints. It takes an
A-local axis/probe angle and an explicit child component, removes the candidate
twist, reattaches the original twist, checks absolute candidate Z swing bounds,
then rotates the component about A's world anchor. It changes pose only and
rejects undefined twist, Hinge mode or out-of-range swing without moving bodies.
Unsupported axes, invalid bounds/probes or parent/child component misuse are
reported as invalid arguments. Floor/contact feasibility remains caller-owned.
This is a bounded pose-projection API, not a general ball-joint dynamics solver.

HipZReplay's constrained variant now calls this production API rather than
duplicating quaternion decomposition. The saved right-hip probe +0.160193 rad
still preserves X twist 0.8, respects the new absolute swing bounds and reduces
thigh overlap below 1e-6. Refined search reaches zero at about +0.1601935 rad;
final left/right swing Z coordinates are about +0.304311/-0.041802 rad. Maximum
leg anchor gap remains below 1e-6, velocities unchanged, and floor no-worsening
holds. The saved left foot remains pre-penetrating; this is not full floor repair.

The joint_swing_twist CTest covers reconstruction, mixed swing/twist, signed and
scaled quaternion invariance, bounded rejection, retained pivot/twist/velocity,
Hinge rejection, zero axes and the undefined-twist singularity. hip_z_replay
additionally checks both hip modes, the fixed saved probe and production Z
bounds before running the copied search and descendant-preservation checks.
All 33 runnable CTests pass after rebuilding their targets; the two pre-existing
outdated floor-signature tests remain excluded. The application and full-body
diagnostic build successfully. Artifact build/swing_twist_metadata_10s.csv
repeats the same room-inclusive hip+knee opt-in run: its 38,400 velocity-sweep
rows, thigh correction/block summaries and room summary exactly match
build/live_class_a_hip_knee_10s.csv. This verifies unchanged live behavior, not
new stability or live swing-limit enforcement.

### Independent Y/Z swing limits and live hip pose projection

The previous checkpoint was committed as 75a4b4f (`Add swing twist hip joints`).
Joint3D now also appends minSwingY/maxSwingY=-0.4/+0.4 rad. Both hips use these
and their existing -0.4/+0.4 Z bounds. SwingAngles, relativeJointSwingAngles and
relativeJointSwingY expose the independent Y/Z components of the shortest swing
rotation vector in A's frame. These are log-quaternion coordinates, not Euler
angles or anatomical calibration. Twist remains the existing X scalar.

applyJointSwingWithComponent now validates BOTH absolute swing coordinates.
correctJointSwingWithComponent clamps rotation-vector Y and Z independently,
reconstructs swing from that vector, reattaches the original twist and rotates
the supplied child component about the parent world anchor. An in-range
coordinate is retained while the other is clamped. Hinge mode and undefined
twist do not project; neither operation changes velocity. Existing component
and X-axis validation remain. A 180-degree undefined-twist state is still not
a generally supported runtime correction case.

The historical 1.433333 s fixture has Y swing about -0.652/-1.388 rad and is
therefore outside the new model. hip_z_replay first checks that the strict API
rejects its old right-hip probe. Its historical Z-only comparison then explicitly
restores unbounded Y on a test skeleton copy; this does not weaken the live hip
bounds or claim that the historical pose is valid under them.

The application's existing hip angular branch now enables swing projection
after twist repair and before its existing whole-body foot-floor lift. Positions
and orientations change together; no geometric velocity compensation or new
angular impulse is introduced. Y/Z angular-limit velocity impulses are not yet
implemented: this milestone is pose-only. Legacy branch callers default to
twist-only behavior so historical diagnostics remain reproducible. The headless
flag --hip-swing-limits selects the same current room-inclusive/local-impulse
configuration, enabling this projection and keeping sphere self-collision off.
Old flags retain historical twist-only stepping unless this flag is added.
No thigh/thigh collision pass is enabled; older opt-in collision experiments
have NOT been reconnected to legal swing requests in this task.

The joint_swing_twist tests cover pure X/Y/Z, X+Y, X+Z, mixed X+Y+Z, independent
Y/Z clamps, unchanged twist and in-range coordinate, pivot/velocity preservation,
and repeat projection roundoff. hip_swing_limits additionally exercises both
hip branches, all descendant anchors and orientations, pose-only state and the
existing floor lift. All 34 runnable tests pass after rebuilding; the two
pre-existing outdated floor-signature tests remain excluded. App/diagnostic build.

Matching ten-second runs: build/hip_swing_baseline_10s.csv uses --room-bounds
--no-sphere-self-collision --live-velocity-metrics; build/hip_swing_limits_10s.csv
uses --hip-swing-limits. Both keep the same timestep, motors, damping, 16 outer
and four inner velocity iterations. Both finite, no attachment/twist-limit
failures, no suspicious anchor impulses, minimum body/foot Y=0, room penetration
and group-fit failures=0. OFF -> ON: maximum completed-step hip swing error
2.713886 -> 1.788139e-7 rad; max anchor gap 9.537907e-7 -> 1.072884e-6;
position distance 8.224778 -> 11.176637; peak KE 801.5656 -> 806.2115 J;
peak angular speed 53.86615 -> 48.48655 rad/s. New Y/Z pose bounds work in the
measured run, but movement remains energetic and self-overlap remains. This is
not general physical stability or a complete ball-joint dynamics solver.

### Opt-in legal thigh/thigh swing collision

The bounded hip checkpoint was committed as 9109bb6 (`Add bounded hip swing
constraints`). `--thigh-swing-collision` now selects a separate single-pair
SwingTwist experiment in the application/headless diagnostic. It remains OFF
by default. This mode cannot be combined with --thigh-self-collision or
--thigh-knee-compensation; the legacy paths are retained for comparisons.
In the diagnostic it implies current hip swing limits/local impulses/room bounds
with sphere projection off. No hand/torso, shin/shin or other pair is resolved.

correctThighOverlapWithSwing uses the closest thigh capsule points and separation
normal to calculate each hip's torque direction, transforms it into pelvis-local
space and projects onto Y/Z. It samples signed full/half/zero requests on both
sides (24 nonzero pairs), capped at 0.01 rad per hip per outer iteration. Both
directions are already force-signed; positive request magnitudes need no extra
right-side sign flip. Every moved leg goes through applyJointSwingWithComponent.
Rejected API requests never mutate the live pose. Feasible improving copies
must retain X twist, all six leg anchors/twist limits and both hips' absolute
Y/Z bounds, and pass per-body floor no-worsening. The best verified copy is
applied. There is no floor lift, knee compensation, velocity modification,
unbounded rotation fallback or new-other-pair check in this search. Coincident
closest points/zero projected axes have no invented separating direction.

ThighSwingCollisionAudit records all sampled API limit rejections and improving
floor/twist/anchor rejections. `blockedCallsWithSwingLimit` and
`blockedCallsWithFloor` count blocked calls containing such rejected trials;
they overlap and are not exclusive causes. Limit-rejected trials are counted
before checking collision improvement, so these counters alone do not establish
the dominant causal blocker. Accepted left/right counts are per hip; a paired
accepted call contributes to both. Max penetration/gaps/foot Y are measured
after completed physics steps. App counters reset with the pose/restart state.

The thigh_swing_collision test covers legal separation, request cap, retained
X twist/anchors/velocities, floor rejection, saturated swing rejection, identical
audit/no-audit choice, and degenerate closest-point handling. App/diagnostic and
all 35 runnable tests build/pass; two existing outdated floor-signature tests
remain excluded. No gains, damping, timestep or iteration count was changed.

Ten-second artifacts use the same bounded hip/room configuration:
build/thigh_swing_off_10s.csv (--hip-swing-limits);
build/thigh_legacy_on_bounded_hips_10s.csv (--hip-swing-limits
--thigh-self-collision); build/thigh_swing_collision_10s.csv
(--thigh-swing-collision). OFF output exactly reproduces the prior bounded-hip
baseline build/hip_swing_limits_10s.csv. Maximum thigh penetration OFF/legacy/new
is 0.5792376/0.1374985/0.4540110; position distance 11.176637/11.262475/13.410398;
max leg gap 1.072884e-6/1.430511e-6/9.830250e-7. All stay finite, minimum foot/body
Y=0, room penetration/group-fit failures=0. Hip swing-limit error is
1.788139e-7/0.007174134/1.788139e-7: the old collision helper bypasses the new
swing bounds, so its lower penetration is not a legal equivalent result.

New path accepts 429 calls (left 276, right 248), blocks 1136 calls, with swing
rejected trials in all 1136 and improving floor rejected trials in 428. Total
limit/floor trial rejections 30395/1631; max request exactly 0.01 rad. Peak KE
1508.858 J, peak angular speed 63.76984 rad/s, no suspicious anchor impulse.
It does NOT pass every live constraint tolerance: left knee first twist-limit
failure at 9.233333 s, max overshoot 2.002716e-5 versus the unchanged 1e-5 gate.
All attachment thresholds pass. The mode reduces overlap versus OFF but leaves
substantial collision and a small knee-limit regression; it remains experimental
and opt-in. Do not claim general stability or complete self-nonintersection,
and do not relax the knee threshold to present this run as passing.

### Knee roundoff and bounded swing blocker audit

`--knee-swing-window` traces pose operations and iteration/step endings from
9.225 through 9.241667 s in the opt-in legal thigh swing configuration. The
original completed-step failure at 9.233333 s was BELOW the knee's zero lower
bound, not above 2.2: final forward/backward knee angle/position operations
leave +3.337860e-6 rad, then backward left-hip component rotation changes it to
-2.002716e-5. The collision search and room pass leave it unchanged. The earlier
9.225 s angle repair also misses zero by -1.681120e-5; its pre-operation state is
saved in tests/fixtures/knee_swing_roundoff_9_225000.csv. The normalized twist
projection length is about 0.006447, near the perpendicular 180-degree swing
singularity, so float quaternion storage amplifies orientation roundoff.

Quaternion stores double components and computes products/normalization in
double. Vec3 and render matrices remain float. relativeJointTwistAngle accumulates
relative quaternion and axial projection in double before returning radians as
float; it retains scale-invariant singularity handling and signed wrapping.
This changes numerical trajectories, not limits, gains, damping or iteration
counts. The knee_precision test repairs the saved pose and tests 256 independent
common leg rotations: max knee twist drift about 1.14e-13 rad, descendant gap
drift <=2e-6, velocities exactly unchanged. Precision does not add missing
transverse hinge locking; those large off-axis knee swings remain a model limitation.

The ten-second opt-in run build/knee_double_live.csv has no gap or limit failures:
left-knee max overshoot 7.286974e-7 versus the unchanged 1e-5 gate, max anchor gap
1.072884e-6, min body/foot Y=0, hip swing error 5.960464e-8, finite throughout.
Max completed-step thigh penetration 0.2482319, position distance 12.91329;
693 accepted calls, 598 blocked, request cap still 0.01. These trajectory changes
do not establish general stability or a before/after guarantee for all poses.

`previewJointSwing` shares request math with the bounded applying API and returns
Y/Z coordinates, undefined-twist status and Ymin/Ymax/Zmin/Zmax bound bits.
`--thigh-swing-blocks` evaluates rejected rotations on hypothetical copies ONLY,
records every trial at the worst blocked event, and counts bound hits among
collision-improving requests. Its trial replay does not choose/apply illegal
motions and tests compare detailed/no-audit selection. Production flags/order
are unchanged; this diagnostic uses the same opt-in legal trajectory.

Worst blocked event is 2.516667 s, iteration 2, penetration 0.2482319.
Left hip (twist,Y,Z)=(0.02845154,-0.3999914,-0.04922154);
right=(-0.1844497,-0.4000000,-0.2689490). Both Y coordinates approach min -0.4;
Z is not saturated in this pose. Left-only request +0.01 lowers penetration to
0.2369818 and is floor-no-worse, but requested Y=-0.4000520 violates Ymin.
Right-only +0.01 requests Y=-0.3957401,Z=-0.2601435 (legal), lowers penetration
to 0.2359037, but fails floor feasibility. No sampled improving request is both
floor-safe and swing-legal. Across all improving trials, bound counts
left(Ymin,Ymax,Zmin,Zmax)=(2988,1668,563,607),
right=(1410,1411,124,2950). Thus the worst pose is a Ymin/floor conflict, while
right Zmax also matters elsewhere. Do not infer that every remaining overlap
has exhausted Z swing or that pelvis motion is already proven necessary.

The precision change also re-exposes the existing pairwise four-part `joint`
regression's attachment conflict on a different trajectory: max ankle gap
0.0060544 against its unchanged 0.001 gate. Do not weaken the gate, restore
float roundoff or tune damping to hide it. This is separate from the repaired
full-humanoid knee-limit event. The experiment remains uncommitted and opt-in;
two older floor-signature tests still cannot compile. Report this remaining
suite failure rather than describing the repository as fully green.

After the precision change, the self-collision-OFF current application baseline
(`--hip-swing-limits`, build/quaternion_precision_baseline_10s.csv) also remains
finite for ten seconds, with no gap/limit failures, min Y=0, max gap 1.092571e-6,
hip swing error 5.960464e-8 and position distance 11.827232. Completed-step thigh
penetration is 0.4483978 OFF versus 0.2482319 ON on the new-precision trajectories.
The detailed audit matches 35,392 velocity sweep records byte-for-byte before
the knee window's output-format change. The final runnable CTest suite is 35/36
passing; only the separate pairwise `joint` ankle-gap gate fails. App and all
runnable targets build. No commit was made for this experiment.

### Copied SwingTwist hip plus knee floor compensation

`--swing-knee-replay` runs the unchanged opt-in legal thigh swing configuration
until 2.516667 s, iteration 2, after the backward sweep and before collision
projection. It emits all 16 body states with 17-digit quaternion precision and
the exact torque-derived pelvis-local right-hip request axis/+0.01f angle,
then exits without changing that checkpoint. The 9,635 velocity sweep records
match the previous blocker-audit trajectory byte-for-byte. The extracted fixture
is tests/fixtures/right_hip_swing_knee_2_516667.csv; skeleton anchors are configured
from the neutral assembly before loading dynamic state.

`aura_hip_swing_knee_replay_tests` (CTest hip_swing_knee_replay) loads this copy,
applies the recorded legal right-hip SwingTwist request, freezes the hip pose,
then rotates only rightShin/rightFoot around the knee pivot. Both +/-0.001 rad
probes are measured: foot Y=-0.010590553 / -0.008637667 versus hip-only -0.009614766.
The negative direction raises the foot. Exponential bracketing within the knee's
available twist range followed by 40 bisections retains a verified feasible upper
endpoint; the smaller final endpoint still fails. Applied float-angle correction
is -0.009778382257 rad. This is the smallest correction bracketed along that
chosen local hinge direction at the current float world-position resolution,
not a proof of a global minimum over arbitrary multi-joint pose changes.

Before / hip only / hip+knee measurements:
thigh penetration 0.248231918 / 0.235903710 / 0.235903710 (exactly unchanged by knee);
hip X twist -0.184449673 throughout;
hip Y -0.400000006 / -0.395740092 / -0.395740092;
hip Z -0.268949002 / -0.260143548 / -0.260143548;
knee angle 1.413895011 / 1.413895011 / 1.404116631;
foot Y +0.006713390 / -0.009614766 / -9.536743e-7;
hip gap 1.192093e-7 / 1.685874e-7 / 1.685874e-7;
knee gap 1.192093e-7 / 1.685874e-7 / 3.576279e-7;
ankle gap 0 / 1.332800e-7 / 1.332800e-7.
Allowed foot Y is -1e-6, the existing flat-floor roundoff allowance, not the
initial positive clearance. Neither floor nor joint tolerances changed.

The replay passes: hip swing/twist and knee/ankle limits respected, both thigh
capsule segments exactly unchanged by knee motion, hip pose exactly fixed,
all 16 linear/angular velocities exactly unchanged, other 13 poses unchanged,
and no body acquires/worsens floor penetration under the existing policy.
Result CSV is build/hip_swing_knee_replay.csv. No runtime knee compensation was
enabled; no production collision mathematics or stepping order was changed.
The runnable suite is now 36/37 passing; only the already-exposed legacy `joint`
ankle-gap regression fails, and the two older floor-signature tests remain
excluded. That separate failure was not modified or reclassified as obsolete.
All current SwingTwist collision/precision work remains uncommitted.

### Opt-in live SwingTwist hip plus knee compensation

`--thigh-swing-knee-compensation` enables legal thigh SwingTwist collision plus
knee floor compensation in both application and headless diagnostic. Default
behavior and --thigh-swing-collision retain compensation OFF; legacy thigh
modes remain separate and mutually exclusive. No new collision pair is added.

After a bounded, collision-improving hip request passes swing bounds, retained
hip twist, leg limits and anchor checks, the new search is eligible ONLY when
its remaining rejection bits are LeftFootFloor/RightFootFloor on a moved leg.
Shin/thigh floor failures, swing-limit failures, twist/anchor failures and
non-improving requests never trigger compensation. Each needed knee probes
+/-0.001, chooses the foot-raising direction, brackets a feasible endpoint
within that knee's remaining twist range and uses 40 bisections retaining the
verified feasible endpoint. It rotates shin+foot only about the knee world
pivot. No legacy helper or its 0.01 knee cap was changed. The hip request cap
remains 0.01; knee search is limited by its real angle range, not that hip cap.

The coupled copy is accepted only after all six leg floor/limit/anchor checks
pass and thigh penetration is exactly unchanged from the hip-only candidate.
The helper changes pose only. Tests verify the saved event's production result
matches the isolated replay exactly, no-audit selection is identical, all 16
velocities remain unchanged, and saturated swing bounds cannot trigger knee
search. Counters distinguish selected hip-only/hip+knee calls from candidate
searches/recoveries/failures; a recovered candidate need not be selected as best.
Swing/floor blocked-call counters overlap and do not imply exclusive causes.

Ten-second comparison, same 120 Hz, motors/gains/damping, room, 16 outer/four
inner iterations and thresholds:
OFF build/swing_knee_off_10s.csv (--thigh-swing-collision);
ON build/swing_knee_on_10s.csv (--thigh-swing-knee-compensation).
The OFF trajectory matches all 38,400 previous blocker-run velocity sweep rows
byte-for-byte. OFF -> ON:
max completed-step thigh penetration 0.2482319 -> 0.2894579;
selected hip-only calls 693 -> 389, selected hip+knee 0 -> 49;
blocked calls containing swing rejection 598 -> 667;
blocked calls containing floor rejection 598 -> 300;
eligible foot-only candidate requests 1965 -> 839;
remaining foot-only candidate rejections 1965 -> 651;
knee-recovered candidate requests 0 -> 188 (22.4% of 839 eligible ON requests);
knee-search failures ON=651; selected maximum knee correction ON=0.1058975 rad;
max leg/anchor gap 1.072884e-6 -> 1.092571e-6;
minimum completed-step foot/body Y=0 for both;
max position distance 12.91329 -> 13.402345; both finite throughout;
no gap or limit failures, hip swing error 5.960464e-8 in both;
peak KE 1241.703 -> 1032.458 J, peak angular speed 76.10616 -> 80.23126 rad/s;
no suspicious local anchor impulses in either run.

The mechanism recovers legal foot-blocked requests without the measured
attachment/floor/limit regressions, but worsens the worst live thigh overlap
on the changed trajectory. Raw counts compare different trajectories; do not
claim a global self-collision improvement, general physical stability, or that
all remaining blocks are swing-limit-only. It stays opt-in and uncommitted.
All runnable/app targets rebuild; CTest 36/37 passes. The unchanged legacy
four-part `joint` ankle-gap failure remains separate and unresolved; the two
older collision-signature tests are excluded. No gate was weakened and that
failure was not classified as obsolete.

`--thigh-contact-replay` is read-only capture on the exact opt-in
`--thigh-swing-knee-compensation` trajectory, including room bounds and live
velocity metrics. It saves the worst completed-step capsule overlap and counts
closing contact velocities using the two closest segment points and the normal
from left thigh toward right thigh. All 38,400 velocity-sweep records match the
previous ON run byte-for-byte. The worst pose is at 8.466667 s: penetration
0.289457947, normal speed -3.698000908 m/s. Of 65 overlapping completed steps,
41 are closing (minimum normal speed -7.7124934); none has a degenerate connector.

`tests/fixtures/thigh_contact_worst_on_10s.csv` stores this full sixteen-part
state. `thigh_contact_velocity_replay` loads it after constructing neutral
skeleton anchors and tests a pose attempt followed by a copied-state-only local
inelastic impulse. At this worst pose, all further pose candidates are blocked
(22 swing-limit rejections and two floor rejections), so the pose phase does not
reduce the existing overlap. The impulse uses world inverse inertia for both
thighs in its denominator and actual response, acts only on the two contacting
thighs, and copies no velocities to descendants. It has magnitude 0.874539793
kg m/s, reduces normal speed to 4.32e-7 m/s and total KE from 931.745751 to
930.128726 J. Predicted work -1.617024327 J and measured -1.617025039 J differ
by 7.12e-7 J. Whole-body horizontal momentum changes by about -5.96e-8 and
+1.19e-7 kg m/s. All poses, six leg gaps and foot heights remain exactly unchanged;
the other fourteen bodies receive no velocity change. Already-separating contact
receives no impulse. This proves a local contact response on one frozen state,
not live convergence, removal of existing penetration, or full-body stability.
No runtime collision impulse has been enabled and the experiment is uncommitted.
Validation for this copied contact milestone: app, diagnostic and all runnable
test targets build; 37/38 runnable CTests pass, including the new replay. The
same legacy `joint` ankle-gap regression remains (0.0060544 versus 0.001);
the two older collision-signature targets remain excluded. No tolerance changed.

`--thigh-swing-contact-impulses` now enables the opt-in legal SwingTwist thigh
geometry + knee-floor compensation mode followed by local thigh normal impulses
in both application and headless diagnostic. The existing geometry-only flag
remains unchanged and default self-collision remains OFF. No other collision
pair, timestep, gains, damping, sweep ordering or iteration count changed.
`resolveThighNormalVelocity` in ThighSelfCollision applies zero-restitution,
frictionless equal/opposite impulses only to the two thighs at their closest
capsule segment points when penetration exceeds 0.001 and normal speed is closing.
Its denominator and actual response use world inverse inertia. It changes no
pose and copies no velocity to descendants. Degenerate normals are reported
without a guessed impulse. Subsequent existing joint sweeps redistribute the
response; no additional sweeps were inserted after this end-of-iteration stage.
The final room contact pass is unchanged and may alter contact velocities again.

The fixture replay now compares this production response to its independently
retained reference impulse exactly, audits energy and momentum, verifies unchanged
poses and all other fourteen bodies' velocities, and tests separating, below-
tolerance and degenerate cases. All runnable/app targets build; 37/38 CTests pass.
The unchanged legacy `joint` ankle-gap regression remains separate; no threshold
was changed. Two older collision-signature targets remain excluded.

Ten-second comparison uses the same 120 Hz, 16 outer/four inner iterations,
hip/knee motors, legal hip bounds, floor response and room pass:
geometry-only: build/thigh_impulse_geometry_only_10s.csv (--thigh-contact-replay);
with impulse: build/thigh_impulse_on_10s.csv (--thigh-swing-contact-impulses).
All 38,400 geometry-only velocity-sweep records match the previous ON trajectory
byte-for-byte. Geometry-only -> geometry+impulse:
max completed-step thigh penetration 0.289457947 -> 0.155861825;
overlapping completed steps 65 -> 51; closing overlapping steps 41 -> 29;
minimum completed-step normal speed -7.7124934 -> -2.6469636 m/s;
self-collision impulses 0 -> 353, largest magnitude 1.04055309 kg m/s;
maximum positive individual collision KE delta 2.47639e-7 J (roundoff);
maximum predicted/measured work discrepancy 4.52914e-6 J;
maximum individual momentum-vector drift 4.95113e-7 kg m/s;
accumulated collision-only delta P=(7.07805e-7,2.62260e-6,-3.31551e-7) kg m/s;
max leg gap 1.092571e-6 -> 9.536743e-7;
minimum completed-step foot/body Y=0 in both;
max position distance 13.402345 -> 10.441753; finite throughout both;
no gap/limit failures, hip swing error 5.960464e-8 in both;
peak KE 1032.458 -> 1157.588 J, peak angular speed 80.23126 -> 53.54753 rad/s;
no suspicious local anchor impulses in either run.

This improves the selected pair's overlap and closing motion on the measured
trajectory without the measured floor/attachment regressions. Remaining closing
steps and 0.155862 overlap still exist; other non-adjacent body pairs are untreated.
The changed trajectory has higher peak KE despite locally dissipative collision
impulses, so do not claim general physical stability or full nonintersection.
The mode remains opt-in and the milestone remains uncommitted.

`--thigh-worst-event` is read-only instrumentation of the same live opt-in
geometry+knee+normal-impulse configuration. It buffers begin-step, integration,
then floor, joint geometry/velocity, thigh geometry, thigh impulse and room
checkpoints for each outer iteration, retaining only the step with the largest
completed-step thigh penetration. It prints nearby completed steps as context.
All 38,400 live velocity-sweep records match build/thigh_impulse_on_10s.csv
byte-for-byte. Physics behavior and candidate selection are unchanged.

Worst completed-step overlap remains at 4.691666667 s, penetration 0.155861825,
normal speed +0.0106573813 m/s (separating). Beginning penetration is 0.137019634
with vn +0.276460558; integration reduces it to 0.131957442 (vn +0.818457604).
Iteration-one floor leaves that geometry unchanged. The first joint geometry
sweep increases penetration to 0.238516033; endpoint velocity solves do not
change poses, so this increase belongs to geometry projection. Six accepted
thigh swing projections then reduce overlap to 0.155861765 by iteration six;
iterations seven through sixteen accept no further projection. At iteration
sixteen, candidate rejections include ten swing-limit and five floor blocks,
with nine non-improving candidates and no twist/anchor blocks. The final thigh
impulse is not applied because vn is +0.0106571289; no thigh impulse is applied
in any of the sixteen iterations of this step. The room pass changes overlap
only at float-roundoff scale. This event is not an inward-velocity integration
overshoot and does not establish speculative contacts as its direct fix.

Nearby completed-step overlap: 4.666667 s zero; 4.675 s 0.0439001322;
4.683333 s 0.137019634; 4.691667 s 0.155861825; 4.700 s 0.0286500454;
4.708333 s zero. This is a transient geometric peak, not indefinitely growing
closing penetration. Earlier onset stages were not traced by this checkpoint.
Full trace: build/thigh_worst_event_trace.csv; unchanged full run:
build/thigh_worst_event_10s.csv. Diagnostic target builds, git diff --check is
clean, and 37/38 runnable tests pass with the same separate legacy joint failure.
No speculative contact or pose-solver behavior change was implemented.

`--thigh-joint-geometry-trace` replays the unchanged opt-in thigh swing+knee+
normal-impulse trajectory to 4.691666667 s, outer iteration one, then stops
immediately after its first forward sweep. It records all thirty angle/position
operations, their thigh penetration and joint gap before/after, both hips' X
 twist and Y/Z swing, observed moved parts and cut-component coverage. The
17,984 prior velocity-sweep records match the live impulse baseline prefix
byte-for-byte. Full trace: build/thigh_joint_geometry_trace_final.csv; focused
operations: build/thigh_joint_geometry_operations.csv. No runtime policy changes.

The largest increase is NOT a hip attachment translation. Left hip angle/swing
projection changes penetration 0.131957263 -> 0.221312523 (+0.089355260), while
its gap remains 0.009989919 -> 0.009990145. Left hip twist stays ~0.764978;
Y swing is clamped from 0.499750853 to 0.400000036, Z ~0.105468 remains unchanged.
Only the full left thigh/shin/foot component rotates around the hip pivot; no
whole-body floor lift occurs at this operation. Right hip angular projection
later adds 0.015965670 penetration (0.221707672 -> 0.237673342): twist
0.830638111 -> 0.800000012, Y 0.258990943 -> 0.248031497, Z -0.353793830 ->
-0.361561537. It rotates the full right leg. The left/right hip position repairs
add only 0.000395149 and 0.000842899 penetration, respectively. Their gaps close.
No other operation adds more than 0.001 penetration in this forward sweep.

`tests/fixtures/left_hip_swing_projection_4_691667.csv` stores the full sixteen-
part pre-left-hip-angle state. `thigh_joint_projection_replay` compares:
1. exact current angle wrapper: penetration 0.221312523, hip gap 0.009990145;
2. same wrapper then existing legal thigh collision search: unchanged, blocked
   candidates include swing, floor and twist/anchor checks;
3. copied coupled search: required left Y clamp retained, pending left attachment
   translation fraction jointly chosen with bounded <=0.01 rad Y/Z swing requests
   on both hips. It minimizes the unweighted squared sum of target gap and thigh
   penetration (both lengths), with no worsening floor/other attachment or twist
   violations. This is a sampled objective experiment, not a runtime priority.

The third search samples 41 translation fractions and thirteen radial-bounded
Y/Z requests per hip; 2,296 candidates pass intermediate-state feasibility.
Selected alpha=0.225, left request (Y=0,Z=-0.01), right (Y=-0.005,Z=0) gives
penetration 0.207767308 and target gap 0.007742516. Left swing becomes
(Y=0.399864197,Z=0.096007623); right (Y=0.254202157,Z=-0.353640586).
Foot lowest Y is (0.445333839,0.004957974). All velocities are exactly unchanged,
non-target leg gaps/descendant angles are preserved to 1e-6, hip twists preserved.
None of the feasible sampled candidates keeps penetration <= the original
0.131957263 (1e-6 allowance) while enforcing the required left swing bound.
This is a search-budget limitation result, not proof of global infeasibility.

This is an imperfect mid-sweep fixture: right hip X twist remains 0.830638111
(outside +0.8) until its own later solve, and other attachment errors are already
present. The copied coupled search does not worsen these incoming violations;
it does not claim all joint limits or attachments are valid globally. The
existing collision helper's twist/anchor rejection checks also encounter this
pending violation. Do not relax left swing limits or describe the sampled
result as a passing self-collision solution. Reusable production pose/velocity
solvers and application behavior remain unchanged. No speculative contacts,
larger collision step, pelvis motion, damping/gain/threshold changes or commit.
All runnable/app targets build, git diff --check is clean; 38/39 runnable CTests
pass, with the same separate legacy `joint` ankle-gap failure. Two older floor-
signature targets remain excluded. Full copied comparison is recorded in
build/thigh_joint_projection_replay.csv.

`two_hip_projection_replay` loads the same saved pre-first-hip state at
4.691666667 s and remains entirely copied-state/test-only. It computes both
mandatory hip orientation targets before rotating either disjoint leg component,
using parent-frame swing-vector/twist reconstruction. It clamps left Y swing to
0.4 and right X twist to 0.8, retaining unrelated coordinates to 1e-6; rotations
about the fixed parent hip pivots preserve existing attachment-gap magnitudes.
There is no intervening attachment projection or whole-body floor lift.

Measurements (penetration, max hip gap, minimum foot Y):
original (0.131957263,0.012082526,0.005819559);
serial HIP angle/attachment repairs only (0.238516241,0,0.039526880);
both mandatory angular limits together (0.243079096,0.012082519,0.020385087);
joint two-hip constrained search (0,0,0.017607927).
The serial comparator intentionally replays hip geometry only from the same
input, rather than repairing the pre-existing knee/ankle errors; its thigh
penetration matches the earlier forward-sweep trace, but its foot heights must
not be presented as that entire sweep's final foot heights.
Mandatory coordinates: left (X=0.764978528,Y=0.400000036,Z=0.105468325),
right (X=0.800000012,Y=0.258990973,Z=-0.353793860). Both mandatory foot heights
are positive (0.449047327,0.020385087), with all existing leg gaps preserved.

The new search holds these legal X twists fixed and samples each hip's full
legal Y/Z swing domain on a 17x17 grid (83,521 joint combinations plus the
mandatory candidate), then local coordinate refinement with 0.025,0.0125,
0.00625,0.003125 rad probes. In this run it evaluates 83,550 candidates and
529 pass hard hip-limit, all-body no-worsening-floor and descendant-gap checks.
Each orientation candidate closes both hip anchors with child-component
translations before feasibility evaluation; this experiment searches fully
attached hip configurations rather than fractional attachment compromises.
Candidate ranking is lexicographic: thigh penetration first, maximum hip gap
second within 1e-7 detector roundoff. No weighted objective or minimum-motion
regularizer is used. Other self-collision pairs and room constraints are not
included in this single-pair experiment.

Knee compensation is permitted only for otherwise foot-only floor failures.
A test-only probe/bracket/bisection finds the smallest foot-restoring knee
change within its limits; knee motion is never rewarded by the objective.
This run recovers no candidates through knees and the selected solution uses
no knee adjustment. Its left swing is (Y=0.150000021,Z=-0.400000066), right
(Y=0.250000030,Z=-0.349999994); foot heights are (0.021397233,0.017607927).
Both hip gaps and thigh penetration are exactly zero at float measurement
precision. Existing knee/ankle gaps are preserved, not repaired or hidden.
Full descendant relative orientations, legal fixed hip twists, unchanged
linear/angular velocities for all sixteen bodies, and floor feasibility are
checked. The mandatory reconstruction also has explicit unrelated-coordinate
and existing-gap preservation checks.

This proves a feasible two-hip configuration exists for this fixture, not that
the sampled pose is a global/minimum-motion optimum or that applying its large
swing change live is stable. The selected left Z change is substantial (from
+0.105468 to -0.4); this is a broad legal-domain pose search, not a 0.01-rad
runtime correction. No live solver, pelvis motion, contact impulses, motor,
iteration, damping or threshold behavior changed and no commit was made.
Report: build/two_hip_projection_replay.csv. App/diagnostic/all runnable test
targets build; 39/40 runnable CTests pass, with the same separate legacy `joint`
ankle-gap failure. Two older collision-signature targets remain excluded;
git diff --check is clean.
