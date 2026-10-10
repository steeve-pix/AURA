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
Current position corrections use the graph components; velocity corrections use
local two-body impulses. Legacy propagated velocity helpers are diagnostic only.
Their geometry wrappers change pose only and retain existing floor policies; the
generic `correctJointAngleWithComponent` does not decide floor feasibility.

## Debugging and review

Start with what happens, what was expected, and the smallest testable hypothesis.
Reproduce the issue before fixing it when practical.

For simulation failures, isolate the relevant joint or subtree first, then inspect
how it interacts with the complete body. Use measured anchor gaps, angle errors,
relative velocities, floor penetration, and the first failing step to narrow the
cause. Keep diagnostic output focused enough to interpret.

## Current application and regression

The application has one configuration and accepts no physics mode switches.
On macOS run `./build/AURA.app/Contents/MacOS/AURA`; CLion's `aura` target
uses that same executable with no arguments and builds before running.

- `src/app/BodyJoint.*`: ordered connections and motor/display metadata.
- `src/app/SimulationStep.*`: fixed 120 Hz application tick, 16 outer iterations.
- `src/body/JointSolver.cpp`: current joint geometry and four local velocity passes.
- `src/render/BodyRenderer.cpp`: read-only body drawing and owned visual meshes.
- `src/app/BodyDiagnostics.hpp`: current summaries, checks, and energy ledger.
- `tests/runtime_baseline_tests.cpp`: headless ten-second gate using the exact
  application step, with all 105 eligible body pairs enabled.

Geometry changes poses only. Anchor, hinge-axis, and angular-limit velocity
constraints use local physical impulses; no descendant velocity copying.
Keep the forward sweep and reverse sweep starting at the penultimate joint,
then self-collision and the final room pass, in the existing order. Do not
change damping, motors, timestep, gains, or budgets during a refactor.

Historical experiments stay in `tests/full_body_diagnostics.cpp`, with explicit
flag-selected configurations. They deliberately retain their own stepping to
reproduce saved failures. Do not replace these replays with today's solver or
interpret their defaults as the application baseline. Relevant measurements and
flag instructions are archived in [docs/physics-history.md](docs/physics-history.md).
The current structure is explained in [docs/architecture.md](docs/architecture.md).

In reviews, identify what works and distinguish **must fix**, **should improve**,
and **optional** findings. Explain their observable consequences. Preserve the
developer's code where practical; avoid rewrites based only on style preferences.
