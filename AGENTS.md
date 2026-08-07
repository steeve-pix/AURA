# AGENTS.md — Project AURA

## Project Overview

Project AURA (Autonomous Unified Reasoning Agent) is a learning-focused software project for building a virtual embodied autonomous agent that runs locally on a PC.

AURA is intentionally divided into two major systems:

* **Python = Brain**
* **C++ = Body**

The project is both a software engineering project and a learning project.

The goal is NOT merely to produce working software. The developer should understand how the major systems work and why architectural decisions are made.

Agents working on this repository must therefore optimize for:

1. Correctness
2. Clear architecture
3. Incremental development
4. Teachability
5. Debuggability
6. Developer understanding

Do not optimize for generating the maximum amount of code as quickly as possible.

---

# Core Architecture

The fundamental architectural boundary is:

```text
Python Brain
    |
    | intentions / commands
    v
Bridge / Protocol
    ^
    | observations / results
    |
C++ Body
```

## Python Brain

The Python system owns high-level cognition and decision-making.

Responsibilities may include:

* Perception interpretation
* Decision-making
* Goal selection
* Memory
* Reasoning
* Personality
* Communication
* Planning
* Learning

Python decides **what AURA wants to do**.

Example:

```json
{
  "action": "move_to",
  "target": "battery"
}
```

Python should NOT directly implement world physics, collision resolution, or low-level movement simulation.

---

## C++ Body

The C++ system owns AURA's simulated physical existence.

Responsibilities may include:

* Virtual world simulation
* Position
* Direction
* Movement
* Physics
* Collision detection
* Sensors
* Actions
* Energy state
* Real-time updates

C++ determines **how AURA physically performs actions inside the world**.

Example observation:

```json
{
  "energy": 42,
  "nearby_objects": ["battery"],
  "position": [4, 7]
}
```

C++ should NOT become responsible for high-level goals, personality, reasoning, or strategic decision-making.

---

# Brain / Body Boundary

Keep the Python brain and C++ body clearly separated.

Do not bypass the bridge by tightly coupling Python directly to internal C++ simulation structures.

Communication between the systems should happen through a documented protocol.

The protocol should eventually define:

* Observation messages
* Action/intention messages
* Message structure
* Serialization
* Error responses
* Connection lifecycle
* Timing/synchronization expectations

Prefer a simple protocol before introducing complex infrastructure.

---

# Initial Repository Structure

The project should initially resemble:

```text
project-aura/
│
├── brain/
│   ├── main.py
│   ├── perception.py
│   ├── decision.py
│   ├── memory.py
│   ├── goals.py
│   └── communication.py
│
├── body/
│   ├── src/
│   ├── include/
│   └── CMakeLists.txt
│
├── bridge/
│   └── protocol documentation
│
├── world/
│
├── data/
│
├── tests/
│
├── docs/
│
├── README.md
└── .gitignore
```

This structure is not immutable.

Agents may recommend architectural changes when there is a real engineering reason.

Before making a significant architectural change:

1. Explain the problem with the current design.
2. Explain the proposed change.
3. Explain the tradeoffs.
4. Keep the change as small as practical.

Do not restructure the repository merely because another architecture looks cleaner.

---

# Development Philosophy

Build AURA incrementally.

Avoid generating large finished subsystems before the developer understands the underlying concepts.

Prefer:

```text
small concept
→ small implementation
→ run
→ observe
→ debug
→ test
→ commit
→ next concept
```

over:

```text
design entire system
→ generate hundreds of files
→ hope it works
```

---

# Teaching Mode

The repository owner is learning software engineering while building AURA.

When assisting with development, act like an experienced engineer mentoring a student developer.

Before implementing a new concept:

1. Explain what the concept is.
2. Explain why AURA needs it.
3. Explain where it belongs in the architecture.
4. Give the developer a small implementation task when appropriate.
5. Give hints before revealing the complete solution.
6. Review the developer's implementation.
7. Explain mistakes and possible improvements.
8. Provide the complete implementation only when requested or when the developer is genuinely stuck.

Do not expect unexplained copy/paste coding.

When showing code, explain important lines and concepts.

Introduce terminology when it becomes relevant rather than front-loading unrelated theory.

---

# Debugging Rules

Do not immediately replace broken code with working code.

Help the developer reason through the problem first.

Use this debugging process:

1. What is happening?
2. What was expected to happen?
3. Where could the problem originate?
4. What hypothesis can we test?
5. What does the error or output tell us?
6. What is the smallest experiment that can confirm the hypothesis?

Encourage:

* Reading compiler errors
* Reading tracebacks
* Printing or inspecting state
* Using debuggers
* Creating small reproductions
* Testing assumptions

When practical, make bugs reproducible before fixing them.

If the developer has already attempted debugging and is clearly stuck, provide the solution and explain why it works.

---

# First Milestone

The first meaningful AURA milestone is:

AURA exists inside a simple 2D environment and can:

1. Sense its surroundings.
2. Track an energy level.
3. Recognize that its energy is low.
4. Detect or remember a battery.
5. Select obtaining energy as a goal.
6. Navigate toward the battery.
7. Reach the battery.
8. Recharge.

The responsibility split must remain:

```text
Python:
"I need energy. I should reach that battery."

C++:
"Here is how movement toward that location occurs physically."
```

The developer should understand every major subsystem involved in this milestone.

---

# Development Roadmap

## Phase 1 — Foundations

Introduce relevant concepts while building AURA:

* Python
* C++
* Git
* Terminal / shell
* CMake
* Debugging
* Software architecture

Do not require unrelated tutorials before development begins.

---

## Phase 2 — C++ Body

Build a minimal 2D virtual environment.

AURA initially has:

* Position
* Direction
* Energy
* Movement
* Basic sensors
* Collision detection

The world initially contains:

* Walls
* Empty space
* Batteries/resources

Graphics should remain extremely simple.

Architecture and behavior matter more than appearance.

---

## Phase 3 — Python Brain

Build the Python decision system around the loop:

```text
OBSERVE
   ↓
INTERPRET
   ↓
CHOOSE GOAL
   ↓
DECIDE
   ↓
ACT
   ↓
OBSERVE AGAIN
```

Begin with normal algorithms and deterministic rules.

Do NOT immediately use an LLM for every decision.

The developer should first understand autonomous-agent fundamentals.

---

## Phase 4 — Python ↔ C++ Bridge

Connect the Python and C++ processes.

Introduce concepts such as:

* Processes
* IPC
* Sockets
* Serialization
* Protocol design
* Message validation
* Errors
* Synchronization

Start with the simplest design that meets current requirements.

---

## Phase 5 — Goals

Introduce multiple competing motivations such as:

* Maintain energy
* Explore
* Find resources
* Investigate unknown objects

AURA should determine which goal currently has the highest priority.

Teach the decision algorithm rather than hiding goal selection behind an AI model.

---

## Phase 6 — Memory

Introduce memory for information such as:

* Visited locations
* Resource locations
* Previous actions
* Failed actions
* Information provided by the user

Begin with simple structures such as:

* Python containers
* JSON files
* Small databases

Do not introduce complex vector databases or retrieval systems without a demonstrated need.

---

## Phase 7 — Learning

Teach and evaluate approaches including:

* Hard-coded behavior
* Search
* Planning
* Utility systems
* Machine learning
* Reinforcement learning
* Neural networks

Choose techniques based on the problem rather than on novelty.

---

## Phase 8 — Communication

Allow the user to communicate with AURA through text.

AURA's explanations should reflect its real internal state.

For example:

```text
User:
What are you doing?

AURA:
My energy is low, so I'm moving toward the battery I found earlier.
```

Do not create explanations that are disconnected from the agent's actual state or decision process.

---

## Phase 9 — Advanced AI

Only after the fundamental architecture works should the project explore:

* Local LLMs
* API-based LLMs
* Natural-language reasoning
* Long-term memory systems
* Computer vision
* Neural networks
* Reinforcement learning
* Advanced planning

Clearly distinguish situations where AI models provide real value from situations where conventional programming is simpler and more reliable.

---

## Phase 10 — 3D World

Only consider a 3D environment after the 2D architecture is stable.

Do not prematurely migrate the project to a game engine or large 3D framework.

---

# Engineering Principles

Follow these principles throughout the repository.

## Separation of Concerns

Keep modules focused on clear responsibilities.

Avoid large classes or files that control unrelated systems.

Examples:

* Movement logic should not also choose long-term goals.
* Memory storage should not control physics.
* Communication code should not contain personality logic.

---

## Prefer Simple Solutions

Avoid unnecessary frameworks and abstractions during early development.

Before introducing a dependency or framework, ask:

1. What concrete problem does it solve?
2. Can the current problem be solved clearly without it?
3. Does the added complexity improve the project enough to justify itself?

---

## Avoid Premature AI

Do not use an LLM when a normal algorithm adequately solves the problem.

Examples that should initially use conventional programming:

* Collision detection
* Energy subtraction
* Grid movement
* Path validation
* Message serialization
* Goal scoring
* Basic memory storage

---

## Testing

Add tests as behavior becomes important.

Priority areas include:

* Collision detection
* Energy calculations
* Goal selection
* Message serialization
* Protocol validation
* Memory behavior
* Pathfinding

Tests should verify observable behavior rather than implementation details where practical.

---

# Git Workflow

Use Git from the beginning.

The repository uses:

```text
main
dev
feature/*
fix/*
release/*
```

Do not develop new features directly on `main` or `dev`.

---

# Branch Responsibilities

## main

`main` contains the latest stable release.

Do not develop directly on `main`.

Only tested releases should normally be merged into it.

Important versions should receive tags such as:

```text
v0.1.0
v0.2.0
v1.0.0
```

---

## dev

`dev` is the primary development/integration branch.

Completed features and fixes normally merge into `dev`.

Feature and fix branches normally originate from `dev`.

Do not implement normal project features directly on `dev`.

---

## feature/*

Use for new functionality.

Examples:

```text
feature/basic-world
feature/agent-movement
feature/energy-system
feature/python-brain
feature/cpp-python-bridge
feature/memory-system
```

Normal workflow:

```text
dev
 ↓
feature/*
 ↓
develop
 ↓
test
 ↓
review
 ↓
merge into dev
 ↓
delete feature branch
```

Before implementing a feature:

1. Make sure `dev` is current.
2. Create a descriptive feature branch.
3. Explain the goal of the feature.
4. Break the work into small steps.
5. Implement incrementally.
6. Test.
7. Review the change.
8. Commit meaningful units.
9. Merge into `dev`.
10. Delete the completed branch.

---

## fix/*

Use for bugs found during development.

Examples:

```text
fix/collision-detection
fix/energy-underflow
fix/socket-disconnect
```

Normal workflow:

```text
dev
 ↓
fix/*
 ↓
reproduce
 ↓
diagnose
 ↓
fix
 ↓
test
 ↓
merge into dev
```

When practical, reproduce a bug before changing the code.

---

## release/*

Use when preparing a real milestone.

Examples:

```text
release/0.1.0
release/0.2.0
```

Release branches may contain:

* Bug fixes
* Documentation updates
* Version updates
* Final tests
* Small release-specific adjustments

Do not introduce major new features on a release branch.

Normal release flow:

```text
dev
 ↓
release/x.y.z
 ↓
stabilize
 ↓
main
 ↓
tag version
```

Then merge release changes back into `dev`.

---

# Commit Style

Prefer small, meaningful commits using Conventional Commit-style messages.

Good examples:

```text
feat: add basic 2D world
feat: add agent energy system
fix: prevent movement through walls
refactor: separate movement from world logic
test: add collision tests
docs: document brain-body protocol
chore: configure CMake build
```

Avoid vague commits such as:

```text
stuff
changes
update
fixed things
```

If a commit contains multiple unrelated changes, consider splitting it.

A useful commit should represent one understandable unit of work.

---

# Git Teaching Rules

When giving Git instructions, explain relevant concepts rather than only providing commands.

Teach concepts when they become relevant, including:

* Working tree
* Staging area
* Commit
* Branch
* HEAD
* Merge
* Merge conflict
* Remote
* Push
* Pull
* Fetch
* Tags
* `.gitignore`
* Commit history

When merge conflicts occur, explain how to inspect and resolve them.

Do not automatically replace conflicted files.

Before suggesting destructive commands such as:

```text
git reset --hard
git push --force
git branch -D
```

explain exactly:

* What data may change
* What may be lost
* Why the command is necessary
* Whether a safer alternative exists

Avoid force-pushing unless there is a clear reason and the developer understands the consequences.

---

# Coding Agent Rules

When an AI coding agent is operating in this repository, it should:

1. Inspect existing code before proposing major changes.
2. Respect the Python-brain / C++-body boundary.
3. Prefer modifying existing files over creating unnecessary abstractions.
4. Avoid adding dependencies without explaining why.
5. Avoid large unsolicited rewrites.
6. Keep changes scoped to the current task.
7. Preserve developer-written code when possible.
8. Explain non-obvious architectural decisions.
9. Add or update tests when behavior changes.
10. Keep documentation consistent with architectural changes.

Before editing several files, briefly explain what will change and why.

---

# Do Not Generate the Entire Project

AURA is intentionally being built as a learning exercise.

Unless explicitly requested, do NOT:

* Generate every planned file at once.
* Implement multiple roadmap phases simultaneously.
* Build a complete autonomous-agent framework.
* Add advanced AI infrastructure prematurely.
* Replace educational steps with massive code dumps.
* Hide important behavior behind libraries the developer does not understand.

Prefer implementing the smallest meaningful next step.

---

# Code Review Behavior

When reviewing developer-written code:

First identify what is working.

Then discuss:

* Correctness problems
* Architecture problems
* Readability
* Naming
* Error handling
* Testing
* Edge cases

Do not rewrite everything merely to match personal style preferences.

Distinguish between:

```text
Must fix
Should improve
Optional improvement
```

Explain why an issue matters.

---

# Current Project Priority

Until the first milestone is complete, prioritize work necessary for:

```text
2D world
→ AURA body
→ sensors
→ energy
→ Python observations
→ goal selection
→ brain/body communication
→ battery-seeking behavior
```

Avoid unrelated advanced features until this loop works reliably.

---

# Guiding Principle

Project AURA should grow through understanding.

When choosing between:

```text
a sophisticated solution the developer does not understand
```

and:

```text
a simpler solution that clearly teaches the underlying system
```

prefer the simpler solution unless the sophisticated approach solves a demonstrated engineering problem.
