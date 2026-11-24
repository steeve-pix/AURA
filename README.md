# Project AURA

AURA combines a custom C++20 humanoid simulation, Python actor–critic learning,
and a terminal interface. Start here to find the part you want to understand.

The current project checkpoint is **v0.3.0-rc.1**, a development release candidate,
not a claim of stable learned balance. See the [release notes](docs/releases/v0.3.0-rc.1.md)
for validation results and the remaining ankle-attachment regression. The terminal
app retains its separate CLI version and source build identifier.

| Folder | Responsibility |
| --- | --- |
| `src/`, `include/aura/` | C++ math, physics, body, simulation, rendering, and Python bindings; already grouped by subsystem |
| `python/` | Environment, observations, policy, value network, reward, checkpoints, and trainer |
| `python/experiments/` | Controlled learning comparisons and their analysis scripts |
| `python/diagnostics/` | Physical traces, reward audits, and bounded smoke checks |
| `python/historical/` | Earlier hand-tuned balance search and its interactive session |
| `cli/app/` | Command dispatch, history, and CLI update detection |
| `cli/terminal/` | Terminal input, navigation, and capabilities |
| `cli/screens/` | Model library, training setup, training, critic, evaluation, and control screens |
| `cli/ui/` | Shared widgets, charts, previews, and prompts |
| `cli/platform/` | macOS dictation and bundle resources |
| `cli/backend/` | Python workers launched by the CLI |
| `assets/shaders/` | Rendering shaders |
| `tests/` | [Current checks, historical replays, tools, and shared fixtures](tests/README.md) |
| `docs/` | [Architecture, physics history, and research index](docs/README.md) |
| `models/` | Saved checkpoints, `templates/` untrained designs, and `experiments/` isolated experiment outputs |
| `reports/compare/`, `reports/diagnose/`, `reports/report/` | Read-only exports from the corresponding CLI commands |

Other named folders under `reports/` retain controlled experiment outputs.
Recorded traces remain under `docs/traces/`, and research summaries and numerical
results live under `docs/research/`.

## Run and develop

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

cmake -S cli -B cli/build -DCMAKE_BUILD_TYPE=Release
cmake --build cli/build
./cli/build/aura-cli
```

The existing `cli/install.sh` remains the installer. Inside the CLI, `/update`
builds and loads local CLI changes. The [CLI guide](cli/README.md) describes
commands and shortcuts; the [Python guide](python/README.md) maps the learning
code. CLI worker module paths now start with `cli.backend`, while investigation
modules start with `python.experiments` or `python.diagnostics`. Current examples
use the new paths; archived traces retain their original provenance. The package
initializers also preserve older module launch names, so an already-open CLI
can still launch its workers before you reload it with `/update`.

## A useful reading order

1. `python/aura_env.py`: how physical state becomes observations and reward.
2. `python/aura_policy.py`: how observations become motor actions and value predictions.
3. `python/train_nn.py`: how collected episodes update the actor and critic.
4. `cli/backend/training_run.py`: how training, saving, and resuming fit together.
5. `src/sim/AuraSimulation.cpp`: how a simulation step and termination work.

`build/`, `build-release/`, and `cli/build/` are generated build directories.
`.venv/` contains installed Python dependencies; `.idea/` contains local IDE
settings. These are not source-code folders. Their existing paths are retained
because build caches and IDE configurations refer to them. The ignored
`viewer_action.json` and `viewer_observation.json` files are live bridge state
used by the viewer and control command.

Checkpoints, locks, recorded traces, and historical reports are preserved.
Folder organization does not change physics, rewards, network architectures,
training defaults, or checkpoint contracts.

Use `/create` to build and save model designs, and `/experiment` for bounded matched studies. Saved designs and experiments are available separately in `/center`. New training uses independent simulation workers; see [usage and compatibility](cli/README.md#model-designs-and-experiments).
