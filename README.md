# AURA

**AURA (Autonomous Unified Reasoning Agent)** is a local, embodied autonomous-agent project. A Python brain observes and remembers a partially visible world, chooses goals, and builds plans. A C++ body owns the 2D simulation, sensing, energy, pathfinding, action execution, and OpenGL visualization.

The project deliberately uses understandable, deterministic algorithms rather than hiding its behavior behind an LLM.

## Current version: v0.2.0

v0.2.0 moves AURA beyond the original recharge-only milestone. AURA now supports:

- competing `explore`, `investigate`, and `recharge` goals with hysteresis;
- explicit multi-tick plans for investigation and recharge;
- action-result feedback, including navigation and investigation failures;
- investigation of Unknown objects and persistent investigation history;
- generic entity memory for Batteries, Unknowns, and future object types;
- confidence metadata for remembered entities, including confirmation count, recency, and stale state;
- persistent known terrain, entity memory, investigation outcomes, and visit counts;
- separate memory files for different world identities/seeds;
- shortest-path and energy-viability checks when selecting batteries;
- failed-target avoidance and replanning instead of repeatedly choosing impossible destinations;
- developer visualization of goals, known/visited cells, paths, targets, and the active plan.

Active plans are intentionally runtime state and are not persisted. Durable knowledge is saved; an interrupted process chooses a fresh plan from that knowledge after restart.

## Architecture

```text
                         Python brain
                              │
          ┌───────────────────┼───────────────────┐
          │                   │                   │
        Goals              Decision             Memory
          │                   │                   │
          └─────────────── Planning ──────────────┘
                              │
                         action + debug
                              │
                    newline-delimited JSON
                              │
                         C++ body
                              │
       ┌──────────────────────┼──────────────────────┐
       │                      │                      │
     World                  Agent                  Sensors
       │                      │                      │
  maze/objects          position/energy       local/range view
       └──────────────────────┼──────────────────────┘
                              │
                       BFS navigation
                              │
                    one physical step/tick
```

Python decides **what AURA should do**. C++ determines **whether and how the action physically happens**. Every action outcome returns in the next observation, allowing the brain to advance, fail, cancel, or replace a plan using physical evidence.

## Autonomous loop

```text
observe physical state and previous action result
                         ↓
update persistent world/entity/history memory
                         ↓
advance, complete, or fail the active plan
                         ↓
score and choose the current goal
                         ↓
cancel a plan if its goal is no longer compatible
                         ↓
continue an existing plan or construct a new one
                         ↓
issue one action to the C++ body
```

An investigation plan normally contains two steps:

```text
goal: investigate
  1. move_to an adjacent approach cell
  2. investigate the Unknown target
```

The movement step completes only when AURA actually reaches its target. The investigation step completes only after a successful matching action result.

A recharge plan contains one `move_to` step. Recharging itself remains a physical battery-cell interaction owned by C++.

## Memory model

Each generated world reports a stable identity such as:

```text
maze:1337:42x21:b12:u20
```

The brain converts this identity into a dedicated JSON file under `data/`. A different seed or world configuration receives a different file.

Saved memory keeps distinct kinds of knowledge separate:

- `known_cells`: terrain and geometry such as Empty and Wall;
- `entities`: Batteries, Unknowns, and future object types;
- `investigation_history`: durable outcomes of investigations;
- `visit_counts`: movement history.

Failed targets, active goals, and active plans are runtime reasoning state and are not persisted.

New saves use one generic entity representation:

```json
{
  "position": [12, 5],
  "entity_type": "Battery",
  "status": "confirmed",
  "last_seen_step": 120,
  "times_confirmed": 4
}
```

The loader still accepts the earlier battery-only format for migration.

## World and controls

The current application generates a deterministic 42×21 maze using seed `1337`, with 12 Batteries and 20 Unknowns. AURA starts at `(1, 1)` with 100 energy. Each successful cardinal movement costs one energy; entering a Battery cell restores full energy. Investigating an adjacent Unknown currently reveals a Battery.

The OpenGL window displays the maze, AURA, sensor coverage, remembered/visited cells, current route, target, goal scores, energy, and active-plan summary. Close the window to stop the simulation.

## Build and run

Requirements:

- CMake 3.20 or newer;
- a C++20 compiler;
- Python 3.10 or newer;
- macOS OpenGL frameworks (the current executable target is configured for macOS).

Configure and build:

```bash
cmake -S . -B build
cmake --build build
```

Run from the repository root, passing the repository path as the Python brain's working directory:

```bash
./build/body/aura_body "$PWD"
```

## Tests

Run all Python tests:

```bash
python3 -m unittest discover -s tests/python -p "test_*.py" -v
```

Run all configured C++ tests:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite covers goal selection, entity memory, persistence and legacy loading, per-world isolation, brain-process restart behavior, investigation/recharge plan lifecycles, interruption, failure, and replanning, as well as core C++ world, movement, navigation, serialization, and debug-response parsing.

## Project structure

```text
AURA/
├── brain/
│   ├── main.py            # persistent brain process and protocol loop
│   ├── goals.py           # goal scoring, completion, and hysteresis
│   ├── decision.py        # behavior selection and plan construction
│   ├── planning.py        # plans, steps, observation-driven advancement
│   ├── memory.py          # brain-facing memory interface
│   ├── world_memory.py    # generic remembered entities
│   └── memory_store.py    # per-world JSON persistence
├── body/
│   ├── include/           # C++ interfaces
│   └── src/               # simulation, bridge, navigation, rendering
├── bridge/
│   └── protocol.md        # observation/action protocol
├── tests/
│   ├── cpp/
│   └── python/
├── data/                  # per-world persistent memories
└── CMakeLists.txt
```

## Protocol

C++ launches one long-lived Python process and exchanges one JSON object per line:

```text
C++ observation → Python memory/goals/planning → Python action → C++ execution
```

Actions currently include `idle`, cardinal `move`, `move_to`, and `investigate`. The optional `debug` response contains presentation-only goal, map, visit, and plan information; it never controls body physics. See [`bridge/protocol.md`](bridge/protocol.md) for field-level examples.

## Scope

AURA is a learning-focused autonomous-agent system, not a general intelligence product. Its current priorities are reliable embodiment, transparent reasoning, persistent world knowledge, debuggable planning, and a clean brain/body boundary. More advanced learning or language-model features should build on those foundations rather than replace them.
