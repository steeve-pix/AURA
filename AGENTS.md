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
Their wrappers retain existing velocity compensation and floor policies; the
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
with legacy position and angular-limit handling. Use
`--left-limb-components` for the previous subtree-velocity baseline; without either
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
change net linear momentum by zero. Geometry velocity compensation remains
unchanged and still changes net momentum; do not claim general physical stability.

In reviews, identify what works and distinguish **must fix**, **should improve**,
and **optional** findings. Explain their observable consequences. Preserve the
developer's code where practical; avoid rewrites based only on style preferences.

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
./build/aura
./build/aura_full_body_diagnostics
```

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
