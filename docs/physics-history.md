# Physics investigation history

This archive preserves earlier measurements, configurations, and replay instructions.
Statements such as “current”, “application now”, or “uncommitted” describe their
checkpoint, not necessarily today's application. For the active configuration,
read [architecture.md](architecture.md) and the root AGENTS.md.

Historical flags belong to `aura_full_body_diagnostics`; the visual application
has one fixed configuration with all 105 eligible self-collision pairs enabled.

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

The coupled proof above was committed as `7dadaa0` ("Add coupled hip collision
projection replay"). `aura_two_hip_projection_replay_tests <fixture> --local`
now runs a separate copied local experiment, registered as
`local_two_hip_projection_replay`. It bypasses the exhaustive search. After
mandatory hip limit reconstruction, each of 32 iterations evaluates exactly
eight neighbors: +/-0.01 rad on one of the four hip swing coordinates. Both
hip anchors are repaired before candidate evaluation. Feasible candidates are
ranked by penetration, total hip gap, then angular movement, with at most one
accepted neighbor per iteration. A no-op wins ties once no improvement remains.
Twists stay fixed at their repaired legal values, velocities remain exactly
unchanged, and the floor no-worsening and descendant-preservation checks remain.
Foot-only floor failures may use the existing minimal knee compensation search;
no accepted candidate in this fixture needs it.

Local candidates reconstruct from a fixed mandatory component reference, rather
than accumulating float pose transforms. An initial false stall was traced to
float world-anchor addition/subtraction: a candidate measured 1.034699e-6 gap
drift with float intermediate points but 4.340413e-7 with double intermediates.
The local test now computes descendant gap changes with double quaternion/point
intermediates while retaining the same 1e-6 preservation tolerance. Stored body
positions remain floats; production joint math and tolerances are unchanged.

For the saved 4.691667 s fixture, penetration decreases from 0.243079096 after
mandatory repair to 0.186496615 at iteration 6, 0.131252617 at 12, 0.094592214 at
16, 0.022376776 at 24, and zero at 27. All 256 neighbors are evaluated; 27 moves
are accepted, and iterations 28-32 retain the resolved pose. Final hip gaps are
0 and 1.192093e-7, minimum foot Y is 0.005753338, maximum precise descendant gap
drift is 5.257474e-7, and each accepted hip-coordinate change is at most 0.01 rad.
Final swings: left (Y=0.400000036,Z=-0.144531682), right
(Y=0.248990938,Z=-0.343793839). Limits, fixed twists, descendant relationships
and exact velocity preservation pass. This proves copied local convergence,
not live stability or a general multi-contact/self-collision solver. Existing
descendant gaps are preserved, not repaired. Runtime wiring is unchanged.
Report: `build/local_hip_projection_32.csv`. App, diagnostic and runnable test
targets build; 40/41 runnable CTests pass, with the separate legacy `joint`
failure unchanged. The older `physics` and `body_geometry` targets remain
excluded because of outdated floor signatures.

`--coupled-thigh-projection` now enables an opt-in live two-hip pose search in
both the application and full-body diagnostic. Default self-collision remains
OFF. The new mode replaces `correctThighOverlapWithSwing` with
`correctThighOverlapCoupled`, then retains the same local zero-restitution,
frictionless thigh normal impulse. No timestep, solver iteration, motor, damping,
contact impulse or threshold changes accompany it. Knee compensation is not part
of these eight neighbors: each changes only one hip Y/Z coordinate by +/-0.01.

For overlap above 0.001, the helper reconstructs both mandatory bounded hip poses
and repairs both hip anchors, then evaluates eight neighbors from the same input
reference geometry. It rejects floor worsening, hip-bound failures or descendant
anchor drift above 1e-6 (double intermediate anchor measurement). Whole leg
components rotate about their pelvis hip pivots and translate to close the hip
anchors; the pelvis and all other parts stay fixed. These rigid operations
preserve descendant relative orientations by construction. Velocities are never
modified. Candidates rank lexicographically by penetration, total hip gap and
angular movement with 1e-7 measurement roundoff. The mandatory legal base is the
no-op search candidate; at most one improving neighbor is accepted per outer
iteration. An infeasible mandatory base leaves the input unchanged and reports
its blocker; this is not a general coupled all-contact solve. No overlap skips
all reconstruction/search. Searches do not check other self-collision pairs.

The production helper independently resolves the saved 4.691667 s fixture in
27 of 32 calls, with penetration zero, maximum hip gap 1.192093e-7 and minimum
foot Y 0.005753398. The local replay checks eight-candidate budgets, one-coordinate
step bounds, all hip limits, floor no-worsening, descendant gaps and full relative
orientations, exact unchanged velocities, convergence and exact no-overlap pose
preservation. Report: `build/coupled_production_replay.csv`.

The controlled 10-second comparison (120 Hz, 16 outer/four inner iterations,
hip/knee motors, bounded hip swing, room pass) records:
- existing swing+knee+impulse: maximum thigh penetration 0.155861825, 51 overlap
  steps, 29 closing-overlap steps, max leg gap 9.536743e-7, minimum foot Y 0,
  position radius 10.441753, peak KE 1157.588 J;
- coupled search+same impulse: penetration 0.029897988, 34 overlap steps,
  18 closing-overlap steps, 589 accepted neighbors (311 left/278 right), no
  fully blocked or no-improvement calls, 1274 limit-rejected and 339 floor-rejected
  candidates, max leg gap 9.615579e-7, minimum foot Y 0, position radius 8.327415,
  peak KE 1080.954 J.
Both remain finite with no completed-step gap/twist failures; max hip swing error
is 5.960464e-8. Coupled-mode 188 thigh impulses have maximum momentum drift
4.910641e-7 kg m/s and maximum positive KE delta 4.242622e-6 J (roundoff scale).
All 2,227,200 audited local anchor impulses have no suspicious energy injection.
Pose-only projection creates no linear momentum by construction. Residual
0.0299 thigh penetration remains; this is an opt-in single-pair improvement,
not a claim of general humanoid nonintersection or physical stability.
Reports: `build/thigh_current_live_10s.csv` and `build/thigh_coupled_live_10s.csv`.

The coupled self-collision checkpoint was committed as `83cc283` ("Add coupled
thigh self-collision solving"). The next controlled experiment adds
`--thigh-projection-iterations=1|2|4` alongside `--coupled-thigh-projection` in
both app and diagnostic. The default remains one. `correctThighOverlapCoupledBudget`
uses a fixed count, stopping early at the existing 0.001 penetration tolerance
or the first search without an improving neighbor. Each accepted coordinate
change remains capped at 0.01 rad; its reported request is the maximum single
step, not cumulative per-outer-iteration motion. The caller applies the same
single local normal impulse after the geometry budget. No intermediate joint,
contact or velocity solve is inserted between projection steps.

The diagnostic reports accepted projection steps separately from outer calls,
search/candidate counts, and elapsed projection time. Tests compare each of the
three budgets against identical manually bounded single-step sequences, check
candidate/step budgets and exact pose/velocity agreement, and confirm no-overlap
skips all searches. The one-step live trace matches the committed baseline
byte-for-byte excluding the expanded summary counters and timing.

Ten-second results (same 120 Hz, 16 outer/four velocity iterations, gains, limits,
room, floor and normal impulse):
                               1 step          2 steps         4 steps
maximum thigh penetration      0.029897988     0.010714352     0.000328362
completed overlap steps        34              63              4
closing overlap steps          18              58              3
accepted projection steps      589             642             41
projection searches            589             737             41
candidate evaluations          4712            5896            328
no-improvement searches         0               95              0
fully blocked outer calls       0               92              0
limit-rejected candidates       1274            1712            92
floor-rejected candidates       339             405             35
maximum leg anchor gap          9.615579e-7     1.066240e-6     1.066240e-6
minimum foot Y                  0               0               0
maximum position radius         8.327415        9.228723        11.652372
peak KE (J)                     1080.954        1112.822        1055.522
peak angular speed (rad/s)      57.42411        83.80127        84.92314
projection time (s)             0.077081        0.087894        0.019280
whole diagnostic CPU time (s)   75.9158         75.8394         75.8134

All three remain finite with no completed-step joint-gap/twist failures; maximum
hip swing error is 5.960464e-8. No suspicious local anchor energy events occur.
Maximum thigh-impulse momentum drift is respectively 4.910641e-7, 5.057622e-7
and 4.656613e-10 kg m/s; positive impulse KE change is 4.242622e-6 J for one
step and zero for two/four (roundoff scale). Four steps get below the existing
collision tolerance, but increase position radius and angular speeds relative
to one; do not describe this as general physical stability. Two steps produce
shallower but more persistent contact, so more budget does not monotonically
improve every live trajectory metric. Four steps avoid most subsequent collision
work, explaining the lower accepted/candidate counts and search time. These are
scene-dependent timings from concurrent heavily audited headless runs, not an
application/rendering performance benchmark; total CPU differences are noise.
Reports: `build/thigh_budget_{1,2,4}_10s.csv` and corresponding `_timing.json`.
App, diagnostic and runnable targets build. 40/41 runnable CTests pass; the
separate legacy `joint` failure and the two excluded outdated collision-signature
build targets are unchanged. No threshold or legacy test expectations changed.

`--projection-momentum-trace` is a read-only diagnostic requiring
`--coupled-thigh-projection`; use matching budgets 1 and 4. It records whole-body
Px/Py/Pz, COM position and the largest-angular-speed body after motors,
integration, each floor pass, forward/backward geometry+velocity sweeps, thigh
geometry/impulse and room response, preserving actual interleaved ordering.
Per-operation deltas are summed separately into integration, floor, local anchor,
local angular-limit, geometry, thigh impulse, room and motors. Floor points and
normal/damping contributions come from copied response replays; all 307,200
replays in each run match endpoint linear/angular velocities exactly. The peak
angular owner/time/operation is recorded, including the otherwise separate room
pass. No runtime settings or body state are changed by this trace.

`tests/compare_projection_momentum_traces.py` compares these two trace files
using fixed thresholds 0.1 kg m/s for horizontal momentum and 1 rad/s for the
largest angular-speed difference. It reports both accumulated momentum-state
and per-completed-step momentum-change crossings. It independently balances
momentum-source and COM-displacement ledgers and prints a focused JSON report.
The C++ diagnostic builds; paired instrumented runs match uninstrumented physical
summaries and full saved worst-contact body snapshots exactly. Momentum ledger
closure errors are below 9.6e-11 kg m/s, COM closure below 1.3e-13 m; no physics
or legacy test thresholds/expectations changed.

First material horizontal momentum-state divergence: 1.625 s, floor pass 11,
|P4-P1|=0.103565657 kg m/s. Budget 1 has one right-foot point at
(0.444378972, 2.98e-8, 1.801041126); floor changes (Px,Pz) from
(0.231523749,-3.373874754) to (0.223032948,-3.377840370). Its normal impulse
is vertical only (+0.000402301); horizontal delta (-0.008490801,-0.003965616)
comes from body-velocity damping. Budget 4 has no active contact in that pass:
(Px,Pz) stays (0.318380227,-3.337408431). Both runs contact the right foot
in pass 1 of this step, but only budget 1 keeps it contacting through the later
passes. The first completed-step momentum-change difference above 0.1 is the
same 1.625 s step, magnitude 0.141888102; it is not a single 0.1 contact impulse.

Across ten seconds all non-roundoff horizontal momentum comes from floor
responses: total (Px,Pz) change is (-0.515498643,-8.393069174) for budget 1
and (-9.198261868,-0.880263058) for budget 4. Motors, integration, angular-limit
impulses, pose projection and room contribute exactly zero horizontal momentum.
Local anchor roundoff is at most 1.23e-5 per completed step; total anchor drift
is about (-4.87e-5,+6.78e-5) / (-3.14e-5,+7.09e-5) across 2,227,200 solves.
Thigh impulse drift is roundoff only.

First angular-speed difference above 1 rad/s: 1.591666667 s, pass 1 after the
forward joint sweep. Budget 1's maximum is left foot 8.491721 rad/s, budget 4's
is right foot 12.073540 rad/s. At the immediately preceding floor pass, budget 1
has a right-foot normal impulse +4.302234 kg m/s at
(0.019197941,0.001840740,1.968463540); budget 4 has no contact in pass 1
(its right foot contacts later in this step). The full-run peak is left hand
57.424113 rad/s after floor response at 9.066666667 s, pass 1, in budget 1;
in budget 4 it is neck 84.923143 rad/s after a local neck anchor impulse at
4.208333333 s, pass 16, with the head floor-supported. This is not a demonstrated
harder-foot-impact peak. No suspicious local anchor KE injection occurs.

The travel premise needs a distinction: pose-only projection cannot add linear
momentum, but it CAN translate the mass-weighted COM directly. Net ten-second
COM displacement (X,Z) by integration is (-0.253763,-3.631751) /
(-0.919122,-1.828325) for budgets 1/4; ordinary joint geometry contributes
(+3.932246,+2.948785) / (-2.848936,-2.601782); thigh geometry itself contributes
(-0.030617,-0.112213) / (-0.058843,-0.050178). Floor and room geometry have zero
horizontal COM displacement. These are net vector displacements, not path lengths.
Thus altered floor contact explains momentum divergence, while ordinary pose
projection also contributes substantially to bulk travel. Do not attribute all
extra travel to a floor launch or claim four projections are generally physically
realistic from this comparison. The projection count remains unchanged/default 1;
budget 4 stays the best opt-in collision experiment. Reports:
`build/projection_momentum_{1,4}_10s.csv`,
`build/projection_momentum_comparison.json`.


`--joint-com-projection-replay` requires `--coupled-thigh-projection
--thigh-projection-iterations=4`. It instruments every ordinary
`correctJointPositionWithComponents` call without changing live behavior, saves
both sides of the operation with the largest horizontal mass-weighted COM shift,
and replays that one operation using mass-balanced complementary graph components.
COM measurements accumulate in double precision over stored float positions.
The replay checks the original chooser against the captured live endpoint, pose-only
behavior, other joint anchor-gap preservation, and internal component distances.
Floor and collision feasibility are reported, not assumed or required for the
algebraic replay to pass. Active thigh capsules and the experimental filtered COM
sphere proxies are reported separately; the latter are not live collision shapes.

In the four-projection ten-second run, the worst ordinary positional operation is
the forward right ankle at 4.775 s, outer iteration 1. The anchor error is
(-0.40488386,+0.26262522,+0.02762914); component A is the other fifteen bodies
(total mass 15 kg), component B is the right foot (1 kg). The floor-aware chooser
moves A, producing COM delta (-0.37957861,+0.24621114,+0.02590232) m and closing
the 0.48339045 m anchor gap exactly. The balanced copied correction closes the gap
to 5.96e-8 m with COM drift below 6.2e-8 m and leaves velocities/orientations
exactly unchanged. Both candidates have zero thigh penetration and do not worsen
filtered sphere overlap beyond roundoff. However, balanced motion lowers the
right foot from +0.01186198 to -0.23434916 m: it is floor-infeasible. Thus this
worst event validates the mass-balanced translation algebra, but also directly
shows why contact-constrained corrections need a different distribution. No
production position policy or runtime physics is changed by this diagnostic.
The replay reports maximum other-joint anchor-gap drift 2.3842e-7 m and maximum
within-component center-distance drift 4.7684e-7 m. Live physical summary rows and
the saved worst-thigh-contact snapshot match the prior four-projection run
byte-for-byte. `build/joint_com_projection_replay.csv` contains the focused replay
report and all sixteen input body states; the full unchanged live trace is
`build/joint_com_projection_4_10s.csv`. Floor infeasibility is intentionally not
masked by the replay's algebra/pose-preservation pass result.


`aura_directional_joint_projection_replay` / CTest
`directional_joint_projection_replay` loads the saved all-sixteen-body 4.775 s
ankle fixture `tests/fixtures/right_ankle_com_projection_4_775.csv`. Skeleton
anchors are created in the neutral pose before loading dynamic state. No live
solver changes are made. It compares the current parent-side translation,
full mass balancing, and a copied direction-constrained projection: X/Z remain
mass-balanced; each component's floor lower bound restricts its Y translation,
and the opposite component absorbs the remaining relative Y correction. A
next-representable-height adjustment handles float floor-boundary rounding while
retaining the anchor repair; it does not change the existing 1e-6 allowance.

At this event the directional correction gives anchor gap 5.9605e-8 m,
horizontal COM displacement 4.7707e-8 m, vertical COM displacement +0.23434815 m,
and right-foot lowest Y -9.5367e-7 m. Current one-side correction instead moves
horizontal COM 0.38046137 m and vertical COM +0.24621114 m; full balancing moves
COM only at roundoff but violates the floor. All three have zero active thigh
capsule penetration; filtered experimental sphere overlap increases by at most
5.9605e-8 m for the balanced variants. Velocities and orientations are exactly
unchanged; other fourteen anchor gaps change by at most 2.3842e-7 m. The isolated
test checks the saved event's identity, all-body floor no-worsening, gap closure,
horizontal COM neutrality, pose-only behavior, and attachment preservation. This
validates directional distribution on one flat-floor copied state, not a general
multi-contact/self-collision policy or a full-body runtime stability improvement.


`correctJointPositionDirectionally` in ComponentPosition is a pose-only,
flat-Y=0-floor experiment. It sums each complementary graph component's mass,
mass-balances X/Z, and mass-balances Y unless either component's lowest physical
box point restricts the downward share. Both floor inequalities become lower
bounds on the child-side Y translation; the opposite side absorbs the remaining
relative correction. Candidate positions are checked before application, with a
small common Y lift only for float floor-boundary rounding. No velocity or
orientation is changed. It does not evaluate self-collision, wall contacts or
rotational constraints. Production `correctJointPositionWithComponents` remains
the default; `--directional-joint-projection` selects the new policy in the app
and headless solver for all ordinary joint positional operations. It is currently
rejected alongside self-collision experiments to isolate this floor-only policy.
In headless runs pair it with `--hip-swing-limits`; `--joint-projection-ledger`
independently sums COM displacement from position and angular joint geometry.

Matched ten-second self-collision-OFF runs (16 outer / 4 velocity iterations,
room and bounded hip swing enabled, hip/knee motors only) remain finite with no
attachment/limit failures or room penetration. OFF -> ON: max gap
1.092571e-6 -> 9.536761e-7 m, minimum Y 0 -> 0, max body position radius
11.827232 -> 10.774907 m, peak KE 1154.557 -> 1303.002 J, peak angular speed
69.78943 -> 60.30474 rad/s, final horizontal momentum (-2.349282,-4.433886) ->
(-12.451076,+3.366975) kg m/s. No suspicious local anchor impulses occur.
Each run measures 556,800 ordinary position and 556,800 angular operations.
Position-only net COM displacement changes from (-5.007765,+13.487507,+0.305763)
to (-1.28463e-5,+12.370301,-4.14021e-6) m. Thus horizontal positional COM
artifact collapses from 5.0171 m to 1.3497e-5 m. Angular geometry still moves COM:
(-2.398463,+22.902590,+0.616767) -> (-4.032420,+24.729459,+1.324863) m.
Combined ordinary geometry horizontal drift is therefore about 7.46 -> 4.24 m,
not zero, and combined vertical displacement about 36.39 -> 37.10 m.
These are sums of projection displacements, not the final COM trajectory.
Do not claim all geometric COM artifacts or general physical stability are solved.
The new solver stays opt-in because angular projections still move COM and peak
energy rises; the default was not changed based solely on finite-state evidence.
`build/directional_off_10s.csv`, `build/directional_on_10s.csv`, and
`build/directional_projection_comparison.json` retain the matched results.
The saved ankle replay now compares the production solver with its independent
copied reference. Component-position tests also check airborne unequal component
masses and the opposite floor-supported parent-side case. Both focused CTests pass.


`--angular-com-projection-replay` instruments the unchanged
`--hip-swing-limits --directional-joint-projection` ten-second trajectory and
captures the ordinary angular wrapper with the largest horizontal whole-body COM
shift. It records both full-body endpoints; live physical summaries and COM
ledger match the preceding directional-ON run byte-for-byte. The largest event
is forward left hip at 6.8 s, outer iteration 1: COM delta
(+0.408962525,+0.078090459,-0.210133791) m, horizontal magnitude 0.459789688 m.
The copied fixture is `tests/fixtures/angular_com_worst_directional.csv`.

`aura_angular_com_projection_replay` / CTest `angular_com_projection_replay`
loads this fixture after configuring the skeleton in the neutral pose. It derives
the complete world relative correction from captured endpoint orientations,
uses the same child-anchor world pivot as the live hip wrapper, and rotates both
complementary graph components by -s*delta and (1-s)*delta. This joint has component
masses 13 kg and 3 kg. Delta is +1.281859417 rad about world axis
(+0.266525030,-0.949447095,+0.165875211); left hip twist changes 2.08185935 ->
0.799999893 rad. 101 uniform shares in [0,1] plus 201 local refinement samples
are evaluated without modifying velocities. The s=0 reconstructed endpoint agrees
with the captured live positions within 5.9605e-8 m; no floor lift is needed.

For this event sharing does NOT reduce COM displacement. Horizontal COM magnitudes
at s=0,0.1,0.5,1 are respectively 0.459789686,0.578857538,1.309043681,
2.232828673 m; the best sampled/refined share is s=0 (the original child-only
rotation). All 302 candidates are floor-no-worsening and preserve internal
relative orientations, attachment-gap magnitudes and exact velocities. The best
candidate's minimum body Y is +0.310981363 m, gap drift at most 2.007e-7 m.
Opposing rotations around one fixed pivot do not guarantee cancellation of COM
motion for arbitrary component geometry. No rotational mass-ratio rule or live
angular solver change is justified by this fixture.

This snapshot is an intermediate first-iteration state, not an attached/legal
whole-body reference: left hip anchor gap is already 0.419351399 m, and left
ankle/right hip/right knee/right ankle also have pending limit violations. Whole
component rotations preserve those violations; no candidate satisfies the stricter
all-joints-legal criterion. The report separately labels `strictFeasible=0` and
`preservingFeasible=1`. The CTest validates the search reconstruction and geometric
preservation, NOT all-joint feasibility or COM-neutral correction success. Raw
trials and all-body floor checks are in `build/angular_com_projection_replay.csv`.


`aura_angular_com_projection_replay <fixture> --recenter` and CTest
`angular_com_recenter_replay` test horizontal COM recentering on the same saved
6.8 s left-hip event. `tests/AngularCOMRecenterReplay.hpp` uses the actual
production angular wrapper, verifies exact reproduction of captured body
positions, then applies one common X/Z translation to all sixteen bodies based
on double-accumulated before/after COM. It never changes Y, orientations or
velocities. The new code is test-only; no live angular solver or default changes.

Angular-only -> angular-plus-recenter: horizontal COM shift 0.459789688 ->
2.13218e-8 m; vertical COM shift stays +0.078090459 m; hip twist stays
0.799999893 rad; pre-existing hip gap stays 0.419351399 m; minimum body Y stays
+0.310981363 m. Other joint gap changes from the common float translation are
at most 4.48897e-7 m. All Y values, orientations and velocities are exactly
unchanged. Eligible-pair sphere center distances change by at most 4.76837e-7 m;
closest capsule segment distances by at most 9.53674e-7 m (float roundoff).
Thigh capsule penetration remains zero. These proxy-distance measurements
check translation invariance, not general physical nonintersection.

A frozen sequence starts at the saved input and runs sixteen forward/reverse
angular-only sweeps with existing wrappers (480 visits; four nonzero recenters
in the first sweep). No integration, motors, anchor-position repairs, velocity
constraints or contact impulses occur. Existing hip/knee geometric floor lifts
remain part of those wrappers. Raw cumulative horizontal COM displacement is
0.710288212 m; with per-operation recentering it is 9.74496e-8 m. Maximum
per-operation residual is 6.10141e-8 m; Y changes are preserved to roundoff
between the independently projected sequences and are exactly unchanged by
each recenter. Per-recenter gap/capsule-distance changes stay below 1e-6 m;
orientations/velocities/Y are exact. The copied sequence becomes angular-stationary
after the first sweep; it is not a multi-step live stability test or a repair
for the input's pre-existing attachment/limit inconsistencies.

AURA already has room walls/ceiling, so the current environment is not an infinite
floor. This fixture and all copied sequence states retain zero physical-box room
penetration after recentering. That measurement does not make arbitrary X/Z
recentering wall/obstacle-safe. Room/contact feasibility must be considered before
any future runtime use. Both angular replay CTests pass; focused report is
`build/angular_com_recenter_replay.csv`.


`wholeBodyCenterOfMass` and `recenterHorizontallyWithinRoom` in
AuraBodyConstraint implement the environment-feasible horizontal recenter. The
helper measures desired X/Z translation using double COM accumulation, tests
actual physical-box corners against all four RoomBounds walls, and applies the
full translation when safe. Otherwise 24 binary-search rounds find the maximum
safe fraction along that desired vector. Y/orientations/velocities are untouched.
This handles only the axis-aligned room, not arbitrary obstacles. If the input
is already wall-invalid, it applies nothing and reports that condition for the
existing final room solve; it does not claim to repair the initial violation.

The copied `angular_com_recenter_replay` now also puts the 6.8 s angular result
0.1 m from the -X wall. Full recenter would penetrate by 0.308963776 m. The
accepted alpha is 0.244522095, applied translation (-0.100000374,0,+0.0513823554)
m, with zero room penetration. Remaining horizontal COM displacement is
0.347362641 m versus 0.459789693 m requested; increasing alpha by 1e-4 creates
4.19617e-5 m penetration. Joint-gap/capsule-distance changes stay below 5e-7 m;
Y/orientations/velocities remain exact. The full-safe production path agrees
exactly with the previously tested blind recenter on the original far-wall copy.
The test has a coordinate-roundoff bound for the near-wall setup (float positions
near 60 m); no live joint/contact thresholds are relaxed.

`--feasible-angular-recenter` is opt-in in the app and headless diagnostic. It
requires `--directional-joint-projection`; headless use additionally requires
`--hip-swing-limits`. Self-collision stays OFF. After each ordinary angular
wrapper and before the position repair, it recenters using the same room bounds
as the final room pass. It does not recenter floor, room, integration or
self-collision projections. `--joint-projection-ledger` reports the net angular
COM shift plus a separate raw angular/applied recenter ledger, full/partial
counts and initially-outside count. No timestep, sweep count, motor, damping or
velocity rule changes.

Matched ten-second OFF -> ON runs stay finite, with no attachment/limit failures
and zero completed-step room penetration/group-fit failures. Max gap:
9.536761e-7 -> 9.610960e-7 m; min Y: 0 -> 0; max position radius:
10.774907 -> 8.259691 m; peak KE: 1303.002 -> 1239.050 J; peak angular speed:
60.30474 -> 108.8479 rad/s; final Px/Pz: (-12.451076,+3.366975) ->
(+3.016951,+6.500814) kg m/s. All 2,227,200 local anchor impulses report no
suspicious energy injection. The OFF physical summaries reproduce the preceding
directional-ON baseline exactly.
Net angular geometry COM X/Y/Z changes from (-4.032419864,+24.729458742,
+1.324862901) to (-2.601589e-6,+31.383435334,-3.073390e-6) m: horizontal
magnitude 4.244498 -> 4.0266e-6 m. In the ON trajectory, raw angular X/Z drift
(-1.228517180,+0.609073543) is cancelled by measured recenter contribution
(+1.228514578,-0.609076616) m. There are 20,205 nonzero full recenters, zero
partial recenters and zero initially-wall-invalid inputs. This trajectory does
not exercise wall limiting; the copied test does. Position-only horizontal COM
drift also remains roundoff-scale. The higher angular speed and large vertical
projection totals remain unresolved, so this is a pose-drift checkpoint, not
general physical stability; the default remains unchanged.
Both angular replay CTests pass. Reports: `build/wall_recenter_replay.csv`,
`build/feasible_recenter_off_10s.csv`, `build/feasible_recenter_on_10s.csv`, and
`build/feasible_recenter_comparison.json`.


### Angular dynamics after feasible recentering

Read-only `--angular-dynamics-trace` emits completed-step angular maxima.
Optional `--angular-trace-start=SECONDS --angular-trace-end=SECONDS` emits
per-operation maxima, changed body angular velocities/orientation/endpoint impulse
and lever arm, floor normal-impulse replay details, and local anchor work audits
within that window. A finite trace end stops after that completed step; this is
a focused diagnostic, not a full-run stability gate. No runtime mechanics change.
The comparison script is tests/compare_angular_dynamics_traces.py.

Matching ten-second `--hip-swing-limits --directional-joint-projection` runs,
self-collision OFF, with/without `--feasible-angular-recenter`, first differ by
more than 5 rad/s in completed-step maximum omega at 2.141666667 s: neck owns both
maxima, OFF 40.02077 versus ON 48.48174 rad/s. Tracing that and the preceding step
locates the first threshold crossing in this window at outer iteration 4, forward,
head joint, correctLocalJointVelocity (operation index 1262556).
OFF neck omega 39.74653 -> 39.95012; ON 44.01328 -> 45.55692 rad/s.
Neck lowest Y is 0.97291 OFF / 1.02065 ON, so it has no direct floor response;
head is floor-contacting. The ON neck impulse is (-0.00688660,+0.20955837,
-0.05824709) N s, magnitude 0.2176117 versus OFF 0.0304614.
ON neck orientation wxyz is (0.50872377,0.20271577,-0.12811038,-0.82685801);
world anchor lever arm is (0.07104103,-0.04046182,0.03762998) m.
Both lever arms have length 0.09 m; changed direction/pose and velocity residual,
rather than a longer lever arm, distinguish these responses.
ON anchor scalar speed 0.668179 -> 2.48e-7 m/s, OFF 0.0973794 -> -1.86e-7.
ON predicted/measured work -0.145403668/-0.145403594 J plus quadratic energy
+0.0727018295 J agrees with actual total KE change -0.0727027964 J.
Endpoint angular replay errors are exactly zero. Thus the threshold-crossing
impulse redistributes energy into neck spin while reducing total KE; it does not
establish a new effective-mass energy bug. Earlier trajectory/contact differences
remain upstream causes; this identifies the operation crossing the selected
threshold, not the sole origin of the eventual 108.85 rad/s peak.
The head floor impulses in outer iterations 2-4 sum to 0.00655311 N s ON versus
0.00790045 OFF; they are small normal responses, not the neck's large direct kick.
Reports: build/angular_dynamics_comparison.json and angular_dynamics_matched_*.csv.
Feasible recentering stays opt-in. No gains, damping, limits or physics order changed.

### Head/neck anchor residual and angular momentum audit

`--head-anchor-residual-trace` is read-only and stops after the unchanged
2.141666667 s step. It traces outer iterations 1-4 to include the preceding
backward sweep and final room floor response. Run with
`--hip-swing-limits --directional-joint-projection`, with/without
`--feasible-angular-recenter`; self-collision stays OFF. It records head/neck
world anchor velocities separately, their full difference, head-floor linear/
angular response decomposition, and all four forward head anchor impulses.
Whole-body world-origin angular momentum is the sum of orbital position cross m v and
world-transformed local-principal spin; totals accumulate in double precision.
The expected internal impulse torque from any anchor separation is
(anchorB-anchorA) cross impulseOnB. Geometry, masses, gains and ordering are unchanged.

At outer iteration 4, ON anchor-relative speed A/B=0.66185924, C=0.66069018,
D=0.66817950, E(first anchor impulse)=0.13481797 m/s. After four inner anchor
passes it reaches 0.00116235 m/s. OFF values A/B=0.10798744, C=0.10895144,
D=0.09737941, E=0.01845728; after four passes 0.00016518 m/s.
The iteration-4 head-floor response changes the ON head-anchor velocity by
(+0.002490282,-0.001976490,-0.002186298) m/s; neck-anchor delta is exactly zero.
Thus that particular floor pass does not create the large mismatch.

The preceding backward iteration-3 head solve reduces ON relative speed to
3.504e-6 m/s. Its later neck solve reopens it to 0.1037636, then the final
resolveRoomCollisions pass (which includes floor normal impulses) raises it to
0.6618592. That pass gives head momentum (0,+0.42417097,0) kg m/s and angular
velocity delta (-0.5525274,+0.01203728,-0.04233360) rad/s, with no neck velocity
change. OFF preceding-room residual 0.02957659 -> 0.10798744, head impulse
(0,+0.06706238,0). This is an external floor-contact difference, not a wall
clipping effect or a direct recenter velocity change.

Across the selected forward iteration-4 ON head-joint anchor impulse:
delta P=(+1.19209e-7,+1.19209e-7,0) kg m/s (norm 1.68587e-7);
delta L=(+4.82550e-7,-3.06223e-7,-4.62400e-8) kg m²/s
(norm 5.73380e-7). Anchor-gap expected torque norm is about 1.30e-8.
Total KE decreases 0.07270280 J; endpoint replay remains exact.
Both OFF/ON four-impulse audits pass momentum/energy roundoff checks.
This supports contact-driven internal motion redistribution at this event,
without proving all dynamics physically realistic or diagnosing the later peak.
No mass/inertia tuning was performed. The neck currently uses mass 1 and box
principal moments (0.0102,0.015,0.0102); head mass 1 has
(0.17520833,0.17520833,0.18375), so parameter realism remains a distinct topic.

tests/compare_head_anchor_residual_traces.py checks and summarizes the two traces.
Reports: build/head_anchor_residual_comparison.json and
build/head_anchor_residual_off.csv / build/head_anchor_residual_on.csv.
Completed-step neck maxima exactly reproduce the earlier 40.02077 OFF /
48.48174 ON result. Feasible recentering remains opt-in.

### Mass/inertia inventory and selected ON head impulse

`--mass-inertia-inventory` prints all 16 parts' actual masses, physical box sizes
and local COM principal moments, then exits. All current masses are 1 kg, total
16 kg; no anthropometric distribution is configured. Inertia is computed from
these masses and box dimensions, not rendered capsules. `--head-inertia-audit`
requires `--hip-swing-limits --directional-joint-projection
--feasible-angular-recenter`. It includes the inventory, then follows the same
read-only head-anchor replay, auditing the first forward head anchor impulse at
outer iteration 4, time 2.141666667 s, operation 1262556.
The body states, forces, and solver settings remain unchanged.

The exact scalar anchor impulse is reconstructed from pre-solve endpoint
velocities and world-inverse-inertia effective mass, separately from the rounded
mass-times-measured-delta-v diagnostic. Each endpoint's angular impulse is its
world COM lever arm cross its signed impulse. Axial moment is n dot I_world n.
Directional response inertia is 1/(n dot I_world_inverse n). Neither scalar
generally predicts the full 3D delta omega; the exact prediction uses
applyInverseInertiaWorld(body, angularImpulse). This distinction matters on the
head; the neck's impulse axis lies almost entirely in its equal-Ix/Iz plane.

Neck local principal moments (0.0102000013,0.0150000006,0.0102000013) kg m²;
axis moment/response inertia both 0.0102000013. Angular impulse magnitude
0.0160942543 N m s; predicted delta omega
(-0.542050064,+0.380273312,+1.432215452) rad/s, magnitude 1.577867866;
measured (-0.542050362,+0.380273819,+1.432216644), magnitude 1.577869177.
Vector prediction error 1.32913e-6 rad/s.

Head principal moments (0.1752083302,0.1752083302,0.1837499738) kg m²;
axis moment 0.1801569015, directional response inertia 0.1800574809.
Angular impulse magnitude 0.0674932301 N m s; tensor predicted delta omega
(+0.311280608,+0.058689628,+0.200611860), magnitude 0.374946952;
measured (+0.311280251,+0.058689594,+0.200611115), magnitude 0.374946237.
Vector prediction error 8.27124e-7 rad/s. The axis moments differ about 17.66x.
This establishes small neck rotational resistance at this event, not a complete
explanation of the later 108.85 rad/s peak or a prescribed new mass distribution.
No physics values were tuned.

tests/summarize_mass_inertia_audit.py validates positive inventory, box inertia
consistency and <1e-5 rad/s tensor-response error against this exact event.
Reports: build/mass_inertia_inventory.md / .csv,
build/head_inertia_inventory_comparison.json, build/head_inertia_on_event.csv.
The final measured step still exactly reproduces the previous ON neck maximum
48.48174257312 rad/s. Diagnostic build and focused numerical checks pass.

## Generalized body-contact experiment

`CollisionShape.hpp/.cpp` provides sphere, local-Y capsule, and oriented-box
detection, including capsule/box closest points and 15-axis box SAT. Body parts
own shape metadata; physical dimensions supply shape dimensions. Head uses a
sphere, neck/limbs use capsules, torso/pelvis/hands/feet use boxes. Capsules fit
inside the physical dimensions (radius half the smallest dimension); short
capsules collapse to spheres. Box contacts currently use one representative
point, not a clipped multipoint manifold. There is no self-contact friction,
restitution, swept detection, or descendant velocity copying.

`BodySelfCollision.hpp/.cpp` owns articulated contact projection. Both application
and headless diagnostic expose cumulative `--body-self-collision=1|2|3`: 10
arm/core pairs, then 57 including leg/leg and arm/leg, then all 105 non-adjacent
pairs. Pair indices refer to the supplied body, so reset/copy cannot retain stale
part pointers. Skeleton `connections()` shares the existing fixed topology.
Legacy sphere/thigh flags cannot be combined with these modes. The application always uses batch 3; headless historical defaults remain
unchanged. See the corrected baseline below.

Each existing outer iteration runs joints, body-contact pose projection, local
body-contact impulses, then the existing final room pass. The projection has
four accepted-step slots and searches graph cuts along the worst contact's path.
It reuses the coupled thigh candidate, hinge-coordinate probes (0.01 rad), and
hip/shoulder torque-directed requests (at most 0.06 rad). Floor recovery can use
neck/head, elbow/wrist, or knee/ankle hinge adjustments, searching at most 0.256
rad. Complete candidates must improve the active-pair squared penetration error,
preserve attachment gaps, not worsen implemented joint limits, and not worsen
physical-box floor/room penetration. Geometry changes poses only. Hinge transverse
locking was absent in the initial experiment described here. The corrected
configuration below adds actual hinge locking, without anatomical shoulder swing
bounds or relaxed twist/hip swing bounds.

Contact velocity response uses world inverse inertia and one common world point
for equal/opposite endpoint impulses, preserving both linear and angular momentum.
It removes closing normal speed with restitution zero. Neighbouring joints carry
the response through their ordinary local impulse sweeps. Uniform 1 kg masses,
motors, damping, timestep, 16 outer iterations and four inner velocity iterations
are unchanged.

`body_contacts` tests all ordered shape combinations, rotated detection,
containment, local impulse conservation, neutral compatibility, pair filtering,
and articulated arm/core improvement with unchanged zero velocities. The focused
capsule, room, swing/twist, component-angle, local-anchor, and historical thigh
impulse tests also pass. `tests/summarize_body_collision_runs.py` summarizes the
10-second reports and returns failure for residual penetration above the existing
0.001 m tolerance, even though the diagnostic itself exits only on nonfinite state.
Do not equate a finite collision run with nonintersection. Initial generalized
runs preserve attachments and room containment but leave substantial overlaps;
the collision-enabled baseline milestone remains unmet. Reports and comparison
JSON live under `build/body_collision_*`.

Final Release 10-second staged reports (`body_collision_batchN_final_10s.csv`):
batch 1 max penetration 0.381925 m (torso/left hand at 2.858333 s), peak KE
1063.571 J, largest contact impulse 2.174382 N s; batch 2 penetration 0.5520023 m
(left foot/right thigh at 4.0 s), KE 1167.377 J, impulse 3.858729 N s; batch 3
penetration 0.8402498 m (head/torso at 9.091667 s), KE 795.8089 J, impulse
3.876533 N s. All stay finite with no gap/limit failures, maximum joint gap below
1.911e-6 m, minimum Y=0, and zero completed-step room penetration. Maximum positive
contact energy change is below 4.583e-6 J and momentum error below 9.556e-7 kg m/s.
All three fail the 1 mm collision-quality gate. Do not enable by default or claim
the stable collision-enabled baseline is complete. Mass realism was not used as
a prerequisite and Python/control integration has not been added in this change.


### Corrected generalized collision baseline

The initial staged failures above were traced to freely accumulated transverse
rotation in joints labelled Hinge, floor recovery undoing collision separation,
and a velocity dead zone inside the positional tolerance. In body-collision modes
only, `configureBodyCollisionJoints` enables `enforceHingeAxis` on actual hinges.
Geometry removes transverse swing using graph components; a local 2x2 angular
impulse solve removes transverse relative angular velocity with equal/opposite
endpoint impulses. Hips retain bounded SwingTwist. Shoulders remain actual 1-DOF
hinges here; anatomical shoulder ball joints are future work. Legacy/default
configurations retain their previous behaviour.

The fixed four-slot contact search tries smaller coordinate-step fractions and
aims at 0.5 mm internally, retaining the existing 1 mm acceptance gate. Floor
recovery tries distal hinges before parents. If that cannot preserve collision
improvement, the candidate includes the existing minimal common room-containment
translation, exposed as pose-only `projectRoomGeometry`; it is rechecked against
all geometry requirements. This is an external environment projection, not an
internal momentum response. Every overlapping closing contact receives its local
normal impulse, including shallow overlaps inside positional slop. No velocity
copying, extra damping, gain, timestep, mass, or outer/inner iteration changes.

Corrected Release/O1 ten-second reports are
`build/body_collision_fixed_final_batchN_10s.csv`, summarized by
`build/body_collision_fixed_comparison.json`. Batches 1/2/3 have maximum completed
penetrations 0.4503/0.4815/0.5000 mm, zero steps above 1 mm, zero floor/room
penetration, no joint failures, and finite states. Full batch 3: max joint gap
4.78e-7 m, max position distance 7.84 m, peak KE 402.55 J, peak angular speed
24.51 rad/s. Maximum positive contact KE is 7.97e-7 J and contact momentum error
5.34e-7 kg m/s. It accepts 157 common environment projections, at most 0.02622 m.
This is a tested ten-second collision baseline, not general anatomical realism,
standing control, or an arbitrary-scene stability guarantee. The application always uses this full 105-pair configuration with no arguments.
Application physics switches and legacy experiment branches have been removed;
historical comparisons belong to the headless diagnostic executable only.
Headless diagnostic defaults remain unchanged.

The body_contacts regression now covers shallow-contact impulses and folded
hinge repair. Saved full-body replays cover the large head/torso fold, distal
floor compensation, and collision/environment coupled projection. New local
hinge tests audit anisotropic inertia, angular momentum and energy. Room tests
verify pose-only containment preserves velocities. Ten focused Debug tests and
six corresponding built Release tests pass; pre-existing unrelated suite issues
remain separate. No Python integration is included in this fix.

The full 105-pair Debug cross-check also passes for ten seconds: maximum
penetration 0.49973 mm, no steps over 1 mm, maximum gap 4.78e-7 m, zero
floor/room penetration, finite state, peak KE 402.55 J. Debug and optimized
trajectories differ but meet the same unchanged gates. On macOS launch the
rebuilt application with `./build/AURA.app/Contents/MacOS/AURA`.
CLion uses the same target without program arguments and builds before running.
