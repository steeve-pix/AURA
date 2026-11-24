# AURA tests

Tests protect behavior while the implementation changes. Similar filenames do
not necessarily mean duplicate coverage: isolated joints, full-body behavior,
historical failures, and training contracts answer different questions.

| Folder | What belongs here |
| --- | --- |
| `physics/` | Current math, rigid-body, contact, joint, and subtree checks |
| `rendering/` | Camera, mesh, and read-only rendering transforms |
| `simulation/` | Current environment, balance classification, termination, and runtime baseline |
| `learning/` | Current reward quality, actor/critic, PPO experiments, and checkpoint contracts |
| `cli/` | Screen tests, keyboard checks, evaluation, and training UI tests |
| `historical/` | Frozen physics replays and earlier learning/reward experiments |
| `tools/` | Diagnostic executables and trace-analysis scripts, rather than assertion suites |
| `support/` | Shared deterministic Python fixtures, without test cases |
| `fixtures/` | Saved numerical inputs; retained at their original paths |

Historical tests remain part of the full suites. Moving an experiment here does
not invalidate its evidence or make it a current training recommendation.
In particular, `historical/full_body_diagnostics.cpp` intentionally retains its
own stepping; `simulation/runtime_baseline_tests.cpp` checks the application tick.

## Run the suites

From the repository root, using the existing build configurations:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S cli -B cli/build -DCMAKE_BUILD_TYPE=Release
cmake --build cli/build
ctest --test-dir cli/build --output-on-failure

PYTHONPATH=build-release:. python3 -m unittest discover -s tests -t . -p 'test_*.py'
```

Use the Python interpreter with PyTorch installed and build the Python extension
before running the Python suite. Package discovery requires `-t .`; the package
initializer files allow discovery to recurse into each group.

To narrow a check while developing:

```sh
ctest --test-dir build-release -L current --output-on-failure
ctest --test-dir build-release -L historical --output-on-failure
ctest --test-dir build-release -R '^termination_release_replay$' --output-on-failure
PYTHONPATH=build-release:. python3 -m unittest discover -s tests/learning -t . -p 'test_*.py'
PYTHONPATH=build-release:. python3 -m unittest tests.cli.test_evaluation
```

CTest labels are navigation aids. The unfiltered command still runs all
registered tests. Exact termination replay endpoints belong to the validated
Release build; portable termination behavior is checked in both configurations.

## Manual tools and smoke checks

The `cli_*_smoke.py` scripts are explicit checks rather than unittest discovery
modules. Some require the built CLI, a terminal, graphical access, or a prior
binary; read each script's requirements before running it. The registered
`terminal_shortcuts` test uses a bounded fake-worker driver without training.
`historical/cli_balance_smoke.py` exercises the old BalanceLab UI and requires
the corresponding historical CLI; it is not a gate for today's interface.

Trace-analysis scripts in `tools/` accept existing diagnostic output. Their
command-line examples and the build source paths have been updated for this
layout. Existing executable names, fixture data, reports, and checkpoints are
preserved.

## Cleanup baseline

The pre-cleanup Release suite passed 59 of 60 tests. `joint` already failed
`foot remains attached to shin`; that physics failure remains visible and its
tolerances were not changed. The pre-cleanup Python suite ran 232 tests with
one error caused by an evaluation mock targeting an unused environment factory.
That mock now targets the factory actually called by the evaluator, preserving
the fixture's legacy checkpoint contract.

Post-cleanup verification: all 232 Python tests and all 8 CLI tests passed.
Release passed 59/60 tests, including the strict 462/516 termination replay;
Debug passed 58/59 tests. The only failing test in either configuration was
`joint`. Debug verification ran the first 46 tests sequentially and the remaining
13 in parallel. All 60 Release test names and their ordering were preserved.

No identical test files were found. This cleanup organizes coverage rather than
deleting distinct regression checks. Reward formulas, physics, training behavior,
and historical replay expectations are unchanged.


## Independent-worker integration checks

Current collection defaults to isolated spawned workers. The previous shared-arena
fixtures explicitly select `TrainingConfig(collection_mode="historical")` so their
contract does not change. New `learning/test_independent_workers.py` and
`learning/test_experiment_library.py` cover actual process collection, seeds,
resume/restart, gradients, cancellation, explicit experiment methods and evidence.
`cli/test_experiment_ui.py` checks layer editing, Tab navigation and compact review.
Final integration verification: 254 Python tests and 8 CLI suites pass; portable
Debug and strict Release termination replays pass. See the architecture integration
report for bounded CPU/MPS smoke measurements and the existing `joint` limitation.
