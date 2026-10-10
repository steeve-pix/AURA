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

1. Apply the existing hip/knee motors and gravity; integrate at 120 Hz.
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
balance, Python integration, and arbitrary-scene stability remain future work.

## Cleanup verification

The initial cleanup passed 12 focused Debug tests, including the application
baseline. All ten one-second summaries from both the headless gate and rebuilt
visual application match the pre-cleanup run. Maximum joint gap was 4.78e-7,
maximum body penetration 0.000499725, room penetration zero, and maximum body
radius 7.83932 over ten seconds. No physical parameters were changed.
