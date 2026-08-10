# AURA

**AURA — Autonomous Unified Reasoning Agent**

AURA is an experimental embodied AI system built around a simple architectural idea:

- **Python is the brain**
- **C++ is the body**

The Python brain handles high-level reasoning such as goals, memory, decision-making, and exploration.

The C++ body owns the simulated world, movement, collision detection, energy, sensors, navigation, and physical state.

The two systems communicate through a JSON-based brain-body protocol.

---

## Current Version

### AURA v0.1.0

The first release focuses on proving the core AURA architecture inside a simple 2D grid world.

AURA can currently:

- exist as an embodied agent inside a C++ world
- move through the environment
- avoid walls
- consume energy while moving
- recharge using batteries
- sense its immediate surroundings
- detect nearby resources
- send observations from C++ to Python
- keep a persistent Python brain process alive
- select goals based on internal state
- remember discovered battery locations
- invalidate stale battery memories
- track visited positions
- explore less-visited areas
- select high-level movement targets in Python
- use BFS pathfinding in C++ to navigate around obstacles
- execute physical movement one step at a time

---

## Architecture

```text
                         Python Brain
                              │
                 ┌────────────┼────────────┐
                 │            │            │
               Memory        Goals      Decision
                 │            │            │
                 └────────────┴─────┬──────┘
                                    │
                                 Action
                                    │
                             JSON protocol
                                    │
                                    ▼
                              C++ Body
                                    │
          ┌─────────────────────────┼─────────────────────────┐
          │                         │                         │
        World                     Agent                   Sensors
          │                         │                         │
   walls/resources          position/energy          observations
          │                         │                         │
          └─────────────────────────┼─────────────────────────┘
                                    │
                               Navigation
                                    │
                                   BFS
                                    │
                               physical step
```

AURA intentionally separates high-level reasoning from physical execution.

For example:

Python brain:
"Move to the battery at (8, 3)."

C++ body:
"Find a valid path to (8, 3), avoid walls, and move one physical step."

The brain chooses what AURA wants to do.

The body determines how that action physically happens.

## Autonomous Loop

The Python brain follows a simple autonomous-agent cycle:

```text
OBSERVE
↓
UPDATE MEMORY
↓
CHOOSE GOAL
↓
DECIDE
↓
ACT
↓
OBSERVE AGAIN
```

Example recharge behavior:

```text
Energy becomes low
↓
Goal = recharge
↓
Battery currently visible?
│
┌────┴────┐
yes        no
│          │
use it     check memory
│          │
└────┬─────┘
↓
choose target
↓
send move_to
↓
C++ pathfinding
↓
move one step
↓
repeat
↓
reach battery
↓
recharge
```
## Project Structure
```text
AURA/
├── brain/
│   ├── __init__.py
│   ├── main.py
│   ├── decision.py
│   ├── goals.py
│   └── memory.py
│
├── body/
│   ├── include/
│   │   ├── agent/
│   │   ├── bridge/
│   │   ├── navigation/
│   │   ├── render/
│   │   ├── sensors/
│   │   └── world/
│   │
│   ├── src/
│   │   ├── agent/
│   │   ├── bridge/
│   │   ├── navigation/
│   │   ├── render/
│   │   ├── sensors/
│   │   ├── world/
│   │   └── main.cpp
│   │
│   └── CMakeLists.txt
│
├── bridge/
│   └── protocol.md
│
├── tests/
│   ├── cpp/
│   └── python/
│
├── data/
├── docs/
├── CMakeLists.txt
├── README.md
└── .gitignore
```
## Core Components
### Python Brain

The Python side currently contains the high-level autonomous behavior.

Main responsibilities:

```text
brain/
├── main.py       → brain process loop
├── goals.py      → determines what currently matters
├── decision.py   → chooses actions
└── memory.py     → stores learned world information
```

The brain does not directly modify the physical world.

Instead, it sends intentions such as:

{
"action": "move_to",
"target": [7, 3]
}
## C++ Body

The C++ body owns the physical truth of the simulation.

Its responsibilities include:

world state
agent position
movement
collision detection
energy
batteries
sensors
navigation
rendering
communication with the Python process

The Python brain cannot teleport AURA or directly change its position.

The C++ body validates and executes all physical actions.

## World

AURA currently lives inside a 2D grid.

Example:

```text
##########
#........#
#.A......#
#....#...#
#....#B..#
#........#
##########
```

Legend:

A = AURA
B = Battery
# = Wall
. = Empty space

The current world representation uses a flat C++ vector internally.

A 2D position:

(x, y)

is converted into a vector index using:

```text
index = y * width + x
```
## Agent

AURA's physical body currently stores state such as:

position
energy
maximum energy

Movement occurs one grid cell at a time.

Valid movement directions are currently:

north
east
south
west

Successful movement consumes energy.

Blocked or invalid movement does not.

## Energy

AURA currently starts with a maximum energy value.

Each successful physical step consumes energy.

Example:

Energy: 100
move
Energy: 99
move
Energy: 98

When AURA reaches a battery, energy is restored to the configured maximum.

This physical state is owned by C++.

Python only observes it and reasons about it.

## Sensors

AURA currently uses two simple sensing systems.

### Local Sensor

The local sensor observes the four adjacent cells:

```text
        North
          ↑
West ← AURA → East
↓
South
```

This produces information such as:

{
"north": "Empty",
"east": "Battery",
"south": "Empty",
"west": "Wall"
}
### Range Sensor

The range sensor scans a larger square area around AURA.

It can report nearby objects such as batteries and visible cells.

Example:

{
"nearby_objects": [
{
"type": "Battery",
"position": [7, 2]
}
]
}

The sensor has a limited radius, so AURA cannot directly perceive the entire world.

## Goals

AURA currently has simple rule-based goals.

Example:

energy < threshold
→ recharge

otherwise
→ explore

The important architectural distinction is:

Goal:
"I need energy."

Action:
"Move to (7, 3)."

Goals can persist across many simulation steps.

Actions represent the next intention sent to the body.

## Memory

The Python brain currently maintains in-memory knowledge.

AURA can remember:

discovered battery locations
visited positions
how often positions have been visited
known world cells

Example:

```text
Battery seen at (8, 3)
↓
stored in memory

Battery later outside sensor range
↓
AURA still remembers (8, 3)
```

This allows AURA to reason using information that is no longer currently visible.

## Memory Validation

Memory is not automatically assumed to be permanently correct.

If AURA remembers:

Battery at (8, 3)

and later gets close enough to observe that location but the battery is no longer present, the stale memory can be removed.

The logic is:

```text
remembered location outside sensor range
↓
cannot verify
↓
keep memory
```

```text
remembered location inside sensor range
↓
battery still detected?
/     \
yes      no
|        |
keep     forget
```

This introduces a basic distinction between:

perception
remembered knowledge
stale knowledge
## Exploration

When AURA has sufficient energy, it can select an exploration goal.

The first exploration system tracks visited positions.

AURA prefers areas that have been visited less often.

Example:

north → visited 4 times
east  → visited 0 times
south → visited 2 times
west  → wall

AURA prefers east.

When multiple directions are equally unexplored, one may be selected among the best candidates.

The exploration system can also choose less-explored target locations and use the existing navigation system to reach them.

## Navigation

AURA currently uses Breadth-First Search (BFS) for grid navigation.

Python chooses a high-level destination:

{
"action": "move_to",
"target": [8, 4]
}

C++ then calculates a valid path.

Example:

Start:
(2,2)

Target:
(6,4)

Path:
(3,2)
(4,2)
(4,3)
(4,4)
(5,4)
(6,4)

Only the next physical step is executed.

The next observation is then sent back to Python.

This means navigation is continuously reevaluated rather than blindly executing an old route.

## Brain-Body Protocol

Protocol documentation is located at:

bridge/protocol.md

The current protocol uses newline-delimited JSON over redirected process standard input/output.

Conceptually:

C++ Body
│
│ Observation JSON
▼
Python stdin

### Python Brain
│
│ Action JSON
▼
C++ stdout reader

Each message occupies one line.

### Observation Example

The C++ body may send:

{
"position": [2, 2],
"energy": 24,
"sensor_radius": 3,
"north": "Empty",
"east": "Empty",
"south": "Wall",
"west": "Empty",
"nearby_objects": [
{
"type": "Battery",
"position": [7, 2]
}
]
}

The actual pipe representation is compact single-line JSON.

### Action Examples

Idle:

{
"action": "idle"
}

Move one step:

{
"action": "move",
"direction": "east"
}

Move toward a target:

{
"action": "move_to",
"target": [7, 2]
}
## Persistent Brain Process

The C++ body launches the Python brain once and keeps it alive for the lifetime of the simulation.

Conceptually:

```text
Start C++ body
↓
Launch Python brain once
↓
Create Python Memory once
↓

Observation 1 → Action 1
Observation 2 → Action 2
Observation 3 → Action 3
...

      ↓
Simulation ends
```

This allows Python memory to persist across observations.

The body does not launch a new Python process every simulation step.

## JSON

AURA uses nlohmann/json on the C++ side for structured JSON serialization and parsing.

This replaced the project's earlier hand-written JSON string generation and parsing.

The Python side uses Python's built-in json module.

## Rendering

AURA currently uses a terminal renderer.

Example:

```text
##########
#........#
#.A......#
#........#
#......B.#
##########
```

Rendering is intentionally separated from simulation state.

The world does not print itself.

Instead:

World + Agent
↓
TerminalRenderer
↓
terminal output

This separation is important because a future graphical renderer can display the same simulation state without rewriting the core world logic.

## C++ Architecture

Reusable C++ simulation code is built into:

```text
aura_core

The structure is conceptually:

                aura_core
```
               /         \
              /           \
        aura_body       aura_tests

aura_body contains the actual application entry point and Python process integration.

aura_tests exercises deterministic core behavior.

## Requirements

AURA currently requires:

a C++20-compatible compiler
CMake
Python 3
Git

Current major technologies:

C++20
Python
CMake
nlohmann/json
CTest
Python unittest
## Build

From the project root:

```bash
cmake -S . -B build
cmake --build build
```

The exact executable path depends on the platform, compiler, generator, and build configuration.

## Run

Run the generated:

aura_body

executable.

AURA currently expects the Python brain module to be available relative to the project environment.

The Python brain is launched as:

```bash
python -m brain.main
```

or the platform-equivalent configured by the C++ body.

## C++ Tests

Configure and build the project first:

```bash
cmake -S . -B build
cmake --build build
```

Then run:

```bash
ctest --test-dir build --output-on-failure
```

Current C++ tests cover core deterministic behavior such as:

world bounds
collision rules
valid movement
blocked movement
energy consumption
battery recharge
BFS pathfinding
unreachable destinations
## Python Tests

From the project root:

```bash
python3 -m unittest discover -s tests/python -p "test_*.py" -v
```

On systems where the command is python rather than python3:

```bash
python -m unittest discover -s tests/python -p "test_*.py" -v
```

Python tests cover behavior such as:

goal selection
battery memory
duplicate-memory prevention
forgetting batteries
visit tracking
recharge targeting
remembered battery targeting
exploration decisions
## Git Workflow

AURA uses the following branch structure:

### main
↑
### release/*
↑
### dev
↑
feature/* or fix/*
### main

Contains stable released versions.

Normal development does not happen directly on main.

Important releases are tagged:

v0.1.0
v0.2.0
v1.0.0
### dev

Integration branch for completed development work.

### feature/*

Used for new functionality.

Examples:

feature/basic-world
feature/energy-system
feature/pathfinding
feature/memory-system
feature/exploration
### fix/*

Used for isolated bug fixes.

### release/*

Used to stabilize an upcoming release.

Only release fixes, documentation, version changes, and final testing should normally happen here.

## Commit Style

AURA uses concise, behavior-focused commit messages.

Examples:

Add boundary walls to world
Render AURA agent in world
Drain agent energy while moving
Connect C++ body to Python brain
Navigate AURA to brain-selected targets
Validate remembered battery locations
Add BFS pathfinding for grid navigation

Commits should represent coherent working changes rather than arbitrary time intervals.

## Design Principles

AURA follows several core engineering principles.

### Brain and body remain separate

Python decides what should happen.

C++ decides how it physically happens.

### The body owns physical truth

Python does not directly modify:

position
collision state
energy
world cells
### Perception is limited

The Python brain should reason from observations rather than directly reading C++ world internals.

### Memory and perception are different

AURA distinguishes between:

what I can see now

and:

what I remember seeing before
### High-level actions are preferred

The brain can say:

move_to target

instead of micromanaging every physical step.

### Advanced AI is deliberately postponed

Core autonomous behavior is currently implemented using:

rules
memory
scoring
graph search
deterministic algorithms

AURA does not need an LLM to determine whether it should walk toward a battery.

## What v0.1.0 Does Not Include

AURA v0.1.0 intentionally does not yet include:

a graphical 2D window
a 3D environment
realistic physics
multiple autonomous agents
persistent long-term storage
natural-language conversation
computer vision
reinforcement learning
neural networks
LLM-based decision-making
sophisticated planning
advanced personality systems
production networking
distributed execution

The purpose of v0.1.0 is to establish a working autonomous-agent architecture before adding those systems.

## Roadmap

Possible next development stages include:

### v0.2.x — Graphical 2D AURA
real application window
graphical grid rendering
visual agent movement
visual sensors
battery/resource visualization
debugging overlays
### Future cognition
richer goals
goal priority scoring
planning
more advanced memory
memory persistence
world-model reasoning
failed-action memory
uncertainty
### Communication
text conversation with AURA
explanations based on actual internal state
user instructions
natural-language goal creation
## Learning

Explore the appropriate uses of:

search
planning
utility systems
machine learning
reinforcement learning
neural networks
## Advanced AI

Potential future additions:

local LLMs
API-based LLMs
language reasoning
semantic memory
computer vision
multimodal perception

These should be added only where they provide real value.

## 3D World

After the 2D architecture is mature, AURA can migrate into a 3D environment.

The goal is to preserve as much as possible of the existing:

brain
goals
memory
protocol philosophy
decision architecture

while replacing or extending the physical body systems with:

3D coordinates
orientation
3D collision
3D sensors
real-time rendering
richer physics
## Long-Term Vision

AURA is intended to grow from a simple autonomous grid agent into a richer virtual embodied intelligence.

The long-term goal is not simply to create a chatbot inside a game world.

The project explores how separate systems for:

perception
memory
goals
reasoning
planning
communication
learning
physical execution

can work together as one coherent autonomous agent.

## Status

AURA is an active learning and engineering project.

v0.1.0 represents the first working end-to-end architecture:

```text
PERCEIVE
↓
REMEMBER
↓
CHOOSE GOAL
↓
DECIDE
↓
NAVIGATE
↓
ACT
↓
PERCEIVE AGAIN
```

The immediate focus after v0.1.0 is improving the 2D system while preserving the separation between the Python brain and C++ body.