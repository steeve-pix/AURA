# Aura CLI

Source navigation: `app/` owns command dispatch, history, and update detection;
`terminal/` owns input and navigation; `screens/` owns individual screens;
`ui/` holds shared widgets; `platform/` contains dictation resources; and
`backend/` contains the Python workers. Installer scripts, build stamping, and
the CMake entry point remain here. Worker module commands use `cli.backend.*`.
Generated `/compare`, `/diagnose`, and `/report` exports are grouped under
`reports/compare/`, `reports/diagnose/`, and `reports/report/` respectively.

A C++20 command console for the AURA humanoid simulation. The full-screen interface has a
compact welcome header, a persistent AURA status/version header, a fixed bottom composer,
and command/file menus. Beneath the composer, a status line shows Ready/Shell/Running,
elapsed time while running, queued inputs, completed/failed/stopped runs, and the
most recent model activity (or the current learning setup before any model run).
The welcome screen shows the learning setup and session activity. The persistent
header follows the actual worker lifecycle: running, stopping, finished, failed,
or stopped. Failed work includes a recovery hint; model results are available in
`/center`. Critic-only work identifies the frozen actor; evaluation indicates that
no learning updates occur. These signals describe work launched by this CLI session,
not independent processes elsewhere. Checkpoint counts and build readiness remain
in `/status`; no artifact scans or Python imports are needed to refresh the home summary.
Paths remain available through `/status`, `/files`, and `/cd`; Git branch metadata
is no longer polled or shown in the main interface. The context line stays outside
the gray composer background.
Normal and shell input share the same gray area with equal padding above and
below the text. Main UI text uses the terminal defaults, with soft warm, green,
and lavender colors in the context line; bold/dim text establishes hierarchy. The welcome
screen has a spaced Aura wordmark, learning and activity information, and quick actions
for `/control`, `/train`, and `/center`. Narrow panes use a compact layout without borders.
Shell commands (`!`), file browsing (`@`) and `/voice` dictation on macOS remain available. Optional
`AURA_MOUSE=click` enables mouse actions; this requires Option-drag for selection.
Failures use red text in error messages, completion summaries, and command center
results. User-requested stops remain neutral.
Set `NO_COLOR=1` to use only the terminal's default text colors. No external AI service is connected.

## Saved neural policies

`/train` offers **Resume**, **New model**, **Restart**, **Critic only**, and **Fork actor**.
Resume restores a compatible actor–critic pair, both Adam states, saved sampling RNG state and
cumulative episode/update counts. New model asks for a name and starts fresh;
names are case-insensitive and receive a numeric suffix when already used (including
another active training session). Restart asks for confirmation, resets learning,
and replaces the selected latest and best checkpoints with the fresh starting state.
The selected name and saved episode count remain visible in the training workspace.
One model can be trained by only one CLI session at a time.

Actor training in `/train` uses paired actor–critic learning on balance-quality
reward, with separate Adam optimizers. **Critic only** is a standalone diagnostic:
it trains a fresh return predictor while freezing the actor and does not replace
the paired training critic. The historical Step 121 residual actor–critic pilot
uses an explicit legacy environment, with its own optimizers and checkpoints. Run it
from the repository root with an unused output directory:

```sh
PYTHONPATH=build-release:. .venv/bin/python -m python.experiments.consecutive_actor_experiment --output-dir models/experiments/my-step121-run
```

This runs three consecutive updates for each method and actor seeds 42, 43 and

44. It does not resume or overwrite production models.

`/status` distinguishes these learning modes and reports the starting conditions.
Simulation reset currently uses a fixed upright pose, zero initial velocities
and 0.6 world units of floor clearance. Configurable starting tilts are not yet
implemented; `/train`, model evaluation and `/control` have no starting-posture
option. The neutral-joint reset in `/control` changes joint targets, not the
whole body's starting position or orientation.

Training saves the selected checkpoint in `models/` after each completed batch.
This is the latest state, not a verified best model. It contains
actor and critic weights, both Adam states, actual network layouts, action settings, input
size, reward/termination contract and the cumulative number of completed episodes. Saving replaces
the previous latest checkpoint atomically; an interrupted batch leaves it intact.
The standalone `python/train_nn.py` experiment still starts fresh and writes
`models/aura_balance_env6.pt`; use the CLI for model selection and resumed training.
Project builds use `build-release` with CMake's Release configuration for the
simulator and Python extension, so CLI training uses the optimized simulation.

Open the **Command Center** with Left Arrow on an empty prompt or `/center`.
Its Model library is the default tab: checkpoints on the left, compatibility,
architecture, saved episode/update counts, best score and modified date on the
right. The list uses the available screen height; narrow panes keep the model list.

Each model’s latest checkpoint and its `_best.pt` copy appear together, with **Latest** and green **Best** labels.
Unpaired regular checkpoints are **Saved**;
no missing versions or scores are invented. **B** jumps to the selected model’s
saved best copy (when available). These labels identify checkpoint roles, not
comparisons between experiments or different physics versions.

The library has a live search field: press **/** or **Ctrl+F**, then type a
case-insensitive filename search. Space-separated terms must all match (for
example, `seed42 best`). The matching count updates immediately. **Enter** finishes
editing without opening a model; **Ctrl+U** clears; **Esc** cancels the search edit
and restores the previous query. While editing, Q and action letters are text.
Bracketed paste is supported. Refresh retains the query; B clears it only when
needed to reveal the selected model’s best checkpoint.

Arrow keys select a model; **Enter** inspects, **W** watches actors, **E** evaluates, **T** resumes
training, and **R** refreshes the library. **2** opens current-session model events
and their details; **1** returns to the library. Logs are session-only; saved
checkpoint metadata and evaluation history remain on disk. Model commands are
available through the Command Center. Historical `/models` and `/model` inputs
redirect there. `/eval` and `/evaluate` are no longer commands; use **E** in `/center`. To retain a separate snapshot
before more training, copy the checkpoint to another `.pt` name in `models/`.

Playback reconstructs the saved network, checks input size and action keys against
the simulator, and uses the policy's mean action without exploration noise or
weight updates. It runs one episode at the simulator's normal pace, then holds the
final pose. Close the viewer or press Esc in its window to return to the library.
Checkpoints with an unsupported format, incompatible input/action interface or
different reward/termination contract are rejected with a message; changing default hidden-layer sizes does not invalidate
a checkpoint written in this format.

Select a compatible model and press **E** to evaluate one deterministic episode
and one seeded stochastic episode, without weight updates. The result shows
balanced duration, unbalanced steps, longest balanced interval, recoveries,
average quality, reward, fall indicator and end reason for both modes. Survival
and its previous comparable measurement remain separate diagnostics.
Q stops evaluation and returns to its parent screen; Esc stops it and returns
directly to the main prompt.
The evaluation screen reports runtime startup, checkpoint preparation, both
rollout passes and result saving. Elapsed time and an activity indicator keep
updating between worker messages; a quiet worker is labelled as waiting rather
than given invented progress. Arena progress counts shared world ticks for the
current pass. Model compatibility results are reused briefly while model files,
backend sources and the simulation runtime remain unchanged; **R** in the model
picker forces a fresh inspection.
Measurements are stored separately in `models/.evaluations/`; checkpoint files
are unchanged. Comparisons reset if the simulator, observation encoding or
evaluation method or reward/termination contract changes. This is a measurement from the normal reset pose,
not a claim of general balance stability.

`/train` first evaluates and saves the fresh starting policy as a best-model
candidate, before any optimizer update. It then evaluates the updated policy
after every batch is saved. Its
Overview shows training Batch Mean and Best Episode alongside Eval Balanced,
Optimizer and Policy Loss. Deterministic balanced steps rank best checkpoints;
total reward breaks ties. A strictly better result atomically promotes the exact
saved actor–critic pair to `<model>_best.pt`. The Overview shows its balanced
steps, duration and file in both wide and compact layouts.
Resume retains the persisted best score and compares the resumed policy against
it. Restart begins a new training run and replaces the old best with the fresh
starting policy before training. Latest remains
the checkpoint used to continue training; best is the strongest evaluated state
from that run, not a guarantee of balance from other starting poses.
These automatic measurements do not change weights or add training episodes;
Command Center evaluations maintain their own saved comparison history.

The Overview graphs pair training survival with deterministic evaluation history
from the current session. Gold batch means stand apart from individual episodes;
the evaluation graph retains the session's starting score as a gold reference.
Press **G** to switch the right graph between evaluation survival and policy loss.
The best-balance card shows the current balanced-step gap from the saved best.
Model pickers, setup and evaluation share the same header and bottom navigation;
selection highlights span the full row, and long model names end with an ellipsis.
Successful built-in builds show a compact named summary. **Ctrl+O** opens their
full command and build output; failed commands retain visible error guidance.

## Install and launch

Run `./cli/install.sh`, then type `aura-cli` from any directory.
The standalone executable is installed at `~/.local/bin/aura-cli`.
Once running a version with `/update`, use that command after source changes.
The home screen shows **Update available** when its build differs from the local
CLI sources. `/update` builds and atomically installs the newest local code,
then loads it in the same terminal, preserving the current working directory
and saved command history. Pending queued commands or an unfinished draft are
handled before reloading. The transcript starts fresh after the reload.
Failures and cancellation leave the current CLI session available.

This updates local AURA code; it does not fetch Git commits or update the Codex
application. Versions include a source build ID, such as `0.28.0+abcd1234`.
The first upgrade from an older CLI that lacks `/update` needs one manual restart.
Building uses the existing Python 3 installation for a standard-library build
stamp; no Python packages are added. The installer only builds the CLI;
`aura` and Aura.app belong to the simulation and are separate.

Build manually with:

```sh
cmake -S cli -B cli/build
cmake --build cli/build --target aura_cli
./cli/build/aura-cli
```

Requires CMake, a C++20 compiler, system curses/terminfo, and the `tic` terminal
description compiler. macOS includes the terminal tools; Linux builds need the
distribution's curses development and terminal-tool packages.

## Input modes

### Command syntax colors

Shell drafts, submitted shell inputs, and running/completed command headings use
syntax colors: teal commands/functions, green quoted strings, gold options,
numbers and keywords, lavender variables, and gray comments. Python code in a
`python`/`python3` here-document gets Python keyword/string/number highlighting,
including multiline triple-quoted strings. Other here-document bodies are shown
as literal text. These are display-only lexical colors, not execution or validation.

Full commands retain their colors when Details is expanded. Collapsed command summaries
use at most 72 columns (less in narrow panes), ending with an ellipsis when shortened.
Multiline commands show only their opening line plus an ellipsis. Output previews stay gray, and
expanded output uses its existing normal text color. The command bytes, selection,
completion, execution, and layout are unchanged. `NO_COLOR` disables syntax colors.

### Completion and failure guidance

The existing completion menu now accepts bare command names as well as `/` names.
Prefix matches come first, followed by name fragments and close spelling matches.
Suggestions never silently correct a submitted unknown command; its error can
suggest the closest known command for you to choose.

`cd ` and `/cd ` complete directories. `@` completes file references anywhere in
a draft. Shell input completes executables actually found on PATH and literal
file/directory arguments, including after a pipeline or command separator. Tab
inserts the choice at the cursor without replacing surrounding text. Paths with
spaces are quoted. Completion does not run a subprocess, expand shell variables,
or evaluate substitutions. Filesystem scans and menu sizes remain bounded.

For shell input and `cd` paths, **Tab completes and Enter runs the draft as typed**.
The file-reference picker keeps Enter/Tab insertion and directory browsing.
During a running command, Tab retains its existing queue behavior.

Failed shell runs retain the original exit code and output. A recognized error
line and a concise next step appear outside folded Details. Hints distinguish
missing executables, missing Python modules, absent paths, permission problems,
and configuration/build failures. Unknown failures point to the original output
rather than inventing a cause. User-requested cancellation stays neutral. Hints
do not install dependencies, alter permissions, or retry commands automatically.

### Persistent history and undo/redo

Submitted commands are saved immediately and restored at the next launch. Up/Down,
Ctrl+P/N, Ctrl+R, and `history` use the same saved list. Multiline commands are
preserved as single entries. Up to 1,000 recent commands / 4 MiB are retained;
consecutive duplicates are skipped. Concurrent sessions merge their saved commands
under a lock rather than overwriting each other's history.

History is local at `$XDG_STATE_HOME/aura-cli/history`, or
`~/.local/state/aura-cli/history` when XDG_STATE_HOME is not set to an absolute path.
The folder is private and the file uses owner-only permissions. Unsent drafts,
canceled drafts, and canceled queued commands are not saved. Start a command with
a space to omit it from history, or set `AURA_HISTORY=off` for session-only history.
`history clear` (or `/history clear`) removes saved history and the current
session's command recall/search list. Tab completes `clear` after `history `.
The clearing command itself is removed from history too. Existing transcript
output and queued commands remain; commands executed afterward start a new history.
The saved file is cleared under the same lock used by concurrent sessions.
Other already-open sessions may retain their own in-memory recall lists until restart.
`clear` clears the screen, not saved history.

Cmd+Z undoes input edits and Cmd+Shift+Z redoes them in the Aura iTerm2 profile.
In other terminals use Ctrl+Z (or Ctrl+_) to undo, and Escape followed by Shift+Z
to redo. Undo restores text and cursor position, groups consecutive typing/deletion,
and treats paste, completion, and recalled commands as separate edits. Editing
after undo discards the redo branch. Undo never runs or reverses an executed command.
The same editor history follows an unsent next-command draft back to the prompt
when a running command finishes. Ctrl+Y retains its existing erased-text restoration.

### Mac editing shortcuts

The Aura CLI iTerm2 profile supplies these keys without changing the design:

- Option+Left/Right moves by word; Option+Shift+Left/Right selects words.
- Shift+Left/Right selects characters. Typing or pasting replaces the selection.
- Option+Delete erases the previous word; Option+Forward Delete erases the next.
- Cmd+Left/Right moves to the start/end of the current line; add Shift to select.
- Cmd+Delete erases to the start of the current line; Cmd+A selects the draft.
- Cmd+C copies a native mouse selection, or an editor selection if present.
- Backspace/Delete removes the selected range; an unshifted arrow collapses it.

These controls also apply to the next-command draft while another command runs.
Selection uses Aura's existing lavender color. Ctrl+A/E and Home/End operate on
the current line in multiline drafts. Ctrl+Y restores the last erased word/range.
Mouse settings are reapplied when returning from a project screen: wheel events
scroll output, while physical Up/Down recalls history. New profile windows load
updated key mappings; no global iTerm2 preferences or project simulation code is changed.

Shift+Enter inserts a new line in the Aura CLI iTerm2 profile. The gray input
area grows upward to six visible lines, then follows the cursor. Enter runs
the complete draft; Ctrl+J also inserts a new line. This works when editing
the next command during a running operation. Open a new Aura CLI profile window
after updating so iTerm2 loads the Shift+Enter mapping. Other terminals can send
`ESC [13;2u` for Shift+Enter or use Ctrl+J.

| Input      | Action                                                                     |
|------------|----------------------------------------------------------------------------|
| `/`        | Browse local commands; type to filter, Tab to complete, Enter to run.      |
| `!command` | Execute a real shell command in the current directory.                     |
| `@`        | Browse files; Left/Right switch All, Files, and Directories filters.       |
| `@path`    | Preview a text file, or list a directory. Paths with spaces can be quoted. |
| `?`        | Show keyboard shortcuts and input modes.                                   |

Examples: `!git status`, `!pwd`, `@README.md`, `@"file with spaces.txt"`.
Enter or Tab in the file picker inserts a reference; select a directory to
continue browsing. Press Enter again on the completed reference to preview it.
File references are gray in the composer and submitted text, including inline
mentions and quoted paths. A message such as `inspect @src/main.cpp @README.md`
previews both files locally. Each file starts collapsed as a Read entry; click
Show details or press Ctrl+O to open the captured preview, with line numbers.
File previews show up to 120 lines / 64 KiB. Binary files are identified without
printing their contents. File completion scans at most 4096 entries and shows up to 200 matches;
hidden files appear when the filename prefix starts with a dot.

Shell commands run via `/bin/sh -c` with live stdout/stderr, a working indicator,
elapsed time, and the real exit status. Ctrl+C stops the command's process group;
commands that ignore interruption receive termination after two seconds, then a
forced stop after four. Output is bounded to 2 MiB per command, and terminal
control sequences are removed so command output cannot overwrite the composer.
Command output uses compact status dots, Ran / Failed summaries, and an indented
muted body. Empty output explicitly shows `(no output)`. Completed shell output previews its first three wrapped lines
in gray; while running,
the latest six lines remain visible. Collapsed command headings fit one line with
`…` when needed. Expanding Details restores the complete command and readable
output in the normal text color. Longer output is followed by `+ Show details` and the
number of hidden lines. Ctrl+O toggles the latest entry; optional click mode also allows clicking Show details / Show
less.
Ctrl+O toggles the latest file or command output, including while a command
runs. Expanded output shows `- Show less`. Earlier runs can be opened in the
command center. Captured output is retained within the 2 MiB limit; exit status and
timing remain outside the fold. A muted completion line shows Finished, Failed,
or Stopped, elapsed time, and the local finish time; unsuccessful commands also
show their exit code. Resizing recalculates the preview. Plain mode
prints output in full. Keyboard controls also work without mouse capture.
Commands have no interactive stdin: use your normal terminal for programs such
as vim, password prompts, or interactive shells. A shell `cd` is scoped to that
command; use `/cd` to change Aura's workspace.

## Local commands

### Project workflow

Type project commands with or without `/`:

| Command   | Action                                                                                          |
|-----------|-------------------------------------------------------------------------------------------------|
| `run`     | Configure and build the existing `aura` target, then open the simulation viewer.                |
| `build`   | Configure and build the simulation, without launching it.                                       |
| `control` | Open the viewer and the whole-body joint control table.                                         |
| `train`   | Build the Python bridge and open actor or critic training setup.                                |
| `center`  | Open the model library; E evaluates the selected policy without exploration.                    |
| `status`  | Report build artifacts, Python bridge availability, and the age of the last viewer observation. |

Project actions use the AURA repository where this CLI was built, even after
`cd`. Shell commands and file browsing use the current directory. `status`
shows both locations. Recent observation data is not proof that a viewer process
is still running. Build errors stop the workflow before launching the next step.
`run` opens the viewer without rewriting joint targets; `control` uses the existing
controller's preparation behavior.

The initial `/` menu prioritizes these project actions. Console utilities such as
`history`, `queue`, `unqueue`, `files`, `clear`, `version`, and `shortcuts` remain
available by name or by typing their `/` prefix; `help` lists everything.
Unrecognized text produces a command error rather than a chat response. Use
`!` explicitly for shell execution. Dictation only edits the command draft.

### Voice dictation (macOS)

Run `/voice`, allow the macOS microphone and speech-recognition prompts, then
speak. The waveform follows microphone volume and partial transcription appears
inside the composer. **Enter** stops recording, briefly finishes recognition,
and leaves the text in your editable draft. **Esc** or **Ctrl+C** discards this
dictation. Nothing sends automatically; review the draft, then use Enter or Tab.
Output scrolling and Ctrl+O remain available while listening.

Dictation uses Apple's Speech and AVFoundation frameworks with on-device
recognition required. No API key or chat model is needed. A small Aura Dictation support helper gives
macOS a separate microphone/speech permission identity; it does not modify Aura.app.
The recognizer uses your first preferred macOS language. If on-device recognition
is unavailable for that language, Aura shows an error instead of sending audio
to a server. Enable the language in macOS Dictation settings and retry.
Microphone/recognition errors preserve any captured words in the draft.
Each recording is limited to 60 seconds; audio is not saved to disk.
Permissions can be changed under System Settings > Privacy & Security for
Aura Dictation. The installer places the helper in
`~/.local/lib/aura-cli/AuraDictation.app`; deleting that helper folder along with
`~/.local/bin/aura-cli` removes the CLI and its dictation support. Other platforms show an unsupported message.

### Command list

| Command            | Action                                                                                |
|--------------------|---------------------------------------------------------------------------------------|
| `/help`            | List commands and input modes.                                                        |
| `/shortcuts`       | Show keyboard shortcuts.                                                              |
| `/voice`           | On-device macOS dictation into an editable draft, with an audio level animation.      |
| `/control`         | Edit joint vectors and grouped body-part vectors.                                     |
| `/train`           | Train an actor or critic; inspect rewards, prediction errors, weights and evaluation. |
| `/status`          | Show runtime, learning mode, starting pose and model status.                          |
| `/files`           | List files in the current directory.                                                  |
| `/cd <directory>`  | Change workspace; quoted paths and `~` are supported.                                 |
| `/history [clear]` | Show saved commands, or clear saved and current session history.                      |
| `/version`         | Show the CLI version.                                                                 |
| `/clear`           | Clear the transcript and show the welcome area.                                       |
| `/exit`            | Close Aura CLI and restore the terminal.                                              |
| `/update`          | Build, install and reload the newest local CLI code in the same terminal.             |

`quit` and `/quit` alias `exit` and `/exit`. Commands also work as arguments,
for example `aura-cli --help` or `aura-cli status`.

### Critic-only mode

Choose **4 Critic only** in `/train`, then choose a fresh seed-42 actor or a
compatible saved actor. Saved actors are read as a snapshot; their checkpoint,
optimizer and best policy are not modified. This mode starts a fresh seed-43
critic each run, with 32 training episodes, 16 held-out validation episodes,
20 Adam updates at 0.001, discount 0.995 and target return scale 100.

The Critic Training Lab distinguishes collecting training episodes, collecting
validation episodes and fitting the critic. It displays training MSE, validation
MSE, constant-baseline validation MSE and the percentage improvement or decline.
Error graphs use actual update indices 0, 1, 5, 10 and 20; gold dots mark the
constant validation baseline. That constant is fit only to training targets.
Validation episodes never enter the critic optimizer, and actor updates remain zero.

Use **Q** to go back one level or **Esc** to stop and return home, and arrows / Page Up / Page Down to scroll
in smaller panes. Completed runs save `critic.pt` and `report.json` in a unique
`models/critics/critic-.../` folder. The critic checkpoint has `kind: aura_value`,
its own optimizer state, observation schema, return scale and experiment metadata.
It is kept separate from the actor model picker. Cancelling collection does not
save incomplete model artifacts. Run results and artifact paths remain in command
center activity. These standalone artifacts are separate from the paired critic
used for actor advantages in ordinary `/train` sessions.

The critic screen reports its current evidence explicitly: collecting data,
untrained reference (update 0), or held-out error below/not below the constant
baseline. A stopped or failed run is labelled separately. The screen shows the
frozen actor's input/target counts and the fresh critic's 205 / 64 / 64 / 1 value
architecture. A lower held-out prediction error is not a claim that the critic
improves actor learning; critic predictions remain outside actor updates.
`/status` identifies the default actor architecture and points to critic-only
validation rather than inventing a global critic skill level.

### Neural training workspace

`/train` replaces `/train_balance`. It builds the Python extension and opens
Neural Training Lab using the existing local PyTorch experiment in
`python/train_nn.py`. Dashboard controls and telemetry live separately in
`cli/backend/nn_session.py`. The CLI observes environment calls through an adapter;
the training module contains no command handling or dashboard hooks. PyTorch must be installed in the `python3`
environment;
the command reports import errors and does not install packages automatically.

New and resumed training ask for three labeled settings: **episodes per update**,
**training updates**, and **independent workers**. Defaults are **32 / 3 / 4** on
an eight-core machine. Default workers are capped by detected CPU capacity.
Tab/Shift+Tab changes fields, including empty fields; text appends at the end.
Left/Right does not move the insertion point. G selects the learner device.

Workers run isolated simulations in separate processes. They share frozen actor
weights during collection, with self-collisions and ground contacts preserved,
and cannot collide with each other. There is one learner and at most one optional
viewer. Left/Right selects a worker on the training dashboard; H switches its
chart between the selected worker and the current page of up to four workers.
Chart labels and numbered markers distinguish workers beyond color alone.
Balanced duration is the default; G cycles the existing chart modes to survival
and policy loss. Worker curves connect completed episode measurements, not a
continuous physical path. Their horizontal axis is worker-local order within
the displayed last 60 episodes; M is the displayed episode-order aggregate.
Per-update history and worker identities persist in checkpoints.

Episodes need not divide evenly by workers. More workers than episodes are
allowed; only enough processes to collect the requested episodes are spawned.
Above four, a nonblocking warning recommends benchmarking. The process limit is
max(4, min(64, twice detected CPU capacity)); this bounds accidental resource
allocation rather than imposing a learning limit. More processes need not be faster.

A compatible independent checkpoint can resume with a different worker count,
preserving actor, critic, Adam state and history. Changes are recorded between
updates. Historical shared-arena checkpoints remain available for inspection,
watching and evaluation; Fork actor explicitly migrates compatible actor weights
with a fresh critic and optimizers. Exact old collector behavior remains available
for historical tests and replays, rather than being silently reinterpreted.
New collection contract is **6**, with seed scheme **1**, quality **3**, reward
**3**, and FALLEN termination **2**; physics/friction and the 205/32 interface
are unchanged by this integration. Rollout inference runs on CPU in each process;
the selected CPU/MPS/CUDA device runs learner gradients and optimizer updates.

Each completed batch performs one actor update and one critic update. The
network uses 205 observations -> 64 -> 64 -> 32 target outputs, with Normal
exploration (standard deviation 0.25), seed 42 and learning rate 0.001.
The head motor is disabled by default because its current gains oscillate when
the neck motor is also active. See the joint audit and supported configuration in
[the architecture documentation](../docs/architecture.md#human-like-robot-articulation-and-neural-contract) before
training.
The **Learning** tab opens a reward viewer. Its cards show the current episode's
accumulated return, average reward per simulation step (with the last step's reward),
and the current batch's mean return across completed episodes. Live measurements
arrive alongside progress at the existing reporting rate; completed episodes are
always reported, including terminal and truncated episodes.

The reward-history graph shows total return by episode; wide terminals also show
average reward per step. History contains the last 120 completed episodes from the
current session, retaining absolute episode numbers after resume. It does not
invent historical rewards for episodes loaded from a checkpoint. Batch means use
all completed episodes of the current batch, even when the graph window advances.

The active reward is `balance_quality(next_observation)` in `[0, 1]` for
nonterminal transitions, and zero on confirmed FALLEN. Time-limit truncation
retains that transition's quality reward, then ends the finite task. Further
stepping requires reset. Reward V2 penalties and negative absorbing utilities are
not active. Physical quality components remain available for diagnostics.
**R** switches between the reward viewer and learning details.

The paired actor–critic objective uses discounted reward-to-go G (gamma 0.995).
The critic predicts G/100 before fitting the current batch; actor advantages are
`(G - 100 * V) / 1200`, with predictions detached. The policy loss sums the step
log-probability terms and divides by episode count. Separate Adam updates train
the actor and critic. Episode summaries describe behavior before the update;
decreasing loss alone does not establish better balance. Best selection uses
deterministic balanced steps, then total reward. Evaluation reports deterministic
and stochastic balance metrics, and survival remains a diagnostic.

- **1 Overview:** summary cards for batch mean survival, session best, optimizer
  updates and loss; live episode progress, height, uprightness and action targets.
  Connected 2D plots spread measurements across the available width, with labeled
  axes and the latest point marked. Survival also shows each completed batch mean.
  Charts retain the most recent 120 episodes or 60 updates, with original indices.
- **2 Parameters:** aligned weight/bias tensor summaries and a separate inspector
  for any scalar's before/after values, gradient and change. **Up/Down** chooses
  a tensor; **Left/Right** (or H/L) chooses an element; **[ / ]** jumps 64
  elements; **Home/End** selects the first/last element. **F** expands Adam's
  momentum, variance and bias-correction equations. Measurements refer to the
  latest completed update; float32 storage explains small rounding differences.
- **3 Learning:** episode diagnostics with survival steps, total balance returns,
  mean critic-based step advantages and summed log probabilities. Episode means
  summarize the individual step advantages used by the gradient. A connected
  four-stage learning cycle, reward-to-go objective and aligned layer
  equations. Raw Gaussian samples `u` enter the loss; clipped outputs `a` become
  joint targets. Adam and advantage fractions are centered with visible denominators.
  The most recent completed batch stays visible while the next one collects.

**Page Up/Down** or the **mouse wheel** scrolls any page. On Overview and Learning, **Up/Down** also scrolls by one
line. Smaller windows stack panels; a scrollbar indicates content below the viewport. Symbols, subscripts,
arrows and stacked fractions use terminal-width-aware Unicode. Adam uses a
standalone superscript c for bias-corrected moments, avoiding combining-accent
alignment differences in IDE terminals. `AURA_ASCII=1`
provides an ASCII fallback; `NO_COLOR=1` retains emphasis without colored text.

**P** pauses/resumes at a simulation-step boundary. After the configured block
of consecutive batches, the screen waits for review. **N** starts another block
with the same policy and Adam state. **Q** stops the worker and returns to its parent screen; **Esc** stops it and
returns directly to the main prompt. **Ctrl+C** stops the worker.
Training setup defaults to **CPU**. Press **G** to select an available CUDA or
Apple Metal (MPS) device; the active device is shown in the training header.
During training, **D** opens/closes device information (**G** still toggles the
loss/evaluation chart). The panel reports actual actor/critic tensor devices,
completed GPU optimizer updates in the current session, PyTorch tensor memory,
driver/cache memory and a live OS GPU utilization sample when available. GPU
utilization is system-wide and includes other apps; unavailable readings are
shown explicitly. Metal memory is shared memory. An optional background sampler
refreshes the panel about every two seconds, including during optimizer work;
it stops when the panel closes and never runs elevated commands.

The run progress bar measures fully completed updates, including checkpointing
and evaluation, against the configured block. It reaches 100% only after the last
evaluation. Elapsed time excludes pauses; ETA uses the most recent eight complete
updates and says "warming up" until the first completes. **N** starts a new block
and resets these run counters. Episode completion within the current update is
also shown on wide terminals. Estimates can change as episodes become longer.
If a GPU runtime error occurs, AURA discards and retries the current batch on
CPU, reports the switch, and stays on CPU for the rest of that run. It does not
switch back automatically; select GPU again in a later run to retry it.
In the labeled numeric fields, **Tab** moves to the next input; **Ctrl+U**
clears the selected field so you can re-enter its value.
The physics simulation stays on the CPU, so GPU acceleration may not improve
overall speed for AURA's small actor and critic networks. Check the observed
episode throughput before choosing a device for a long run.
Completed batches are saved to the selected model's checkpoint in `models/`. **V** opens a live view of the same
simulation; press **V** again in the terminal
or viewer to return to headless training. Esc or closing the viewer window also
returns to headless mode. Toggling preserves the current episode, policy, Adam
state and pause state. The viewer remains responsive while paused and between
batches. Drawing is limited to 30 frames per second without adding display pacing
to the training loop; rendering can still add overhead. Faster playback is future work.

Use at least 60 columns and 18 rows; 120 columns and 40 rows shows the complete
layout comfortably. Small terminals show a compact summary. Idle screens do not repaint
continuously. Noninteractive/redirected CLI runs complete the configured block
and print a plain report. Use `--batches 1` with `cli.backend.nn_session` for one batch. Direct runs also work:

```sh
PYTHONPATH=build-release python3 -m python.train_nn
```

Run `python3 tests/cli/cli_nn_smoke.py` for real PTY integration coverage (two batches,
parameter inspection, pause/resume, scrolling, navigation, resizing and cancellation).
Run `ctest --test-dir cli/build --output-on-failure` for screen regressions covering
Unicode widths, malformed telemetry, stale snapshots and compact layouts.
The `terminal_shortcuts` test exercises the production prompt, joint controls,
training setup, training dashboard, evaluation and critic cancellation with
memory-only controls and a fake worker. It performs no learning updates and
writes no checkpoints. It checks both CSI and SS3 arrow encodings, editing,
pause/resume, visual commands, inspection, chart/reward toggles, and Q/Esc navigation.
The real model-library evaluation and filter tests run through
`tests/cli/cli_command_center_smoke.py`; file filters, scrolling and immediate command
cancellation have separate `cli_file_filter_smoke.py`, `cli_scroll_smoke.py`, and
`cli_home_smoke.py` checks. Native clipboard and microphone behavior still depends
on terminal mappings and macOS permissions; these tests do not change either.
The older `tests/historical/cli_balance_smoke.py` targets the retired command and is retained
as historical replay coverage, alongside `python/historical/train_balance.py` and
`python/historical/balance_session.py`.

Historical controller experiments live in `python/historical/train_balance.py`; viewer pacing and
CLI progress reporting live in `python/historical/balance_session.py`. Calling
`evaluate_feedback(simulation, kp, kd)` directly still works without the UI.

The search runs without a 3D window. Only selecting a trial and pressing Enter
opens the viewer, which replays the same controller at watchable speed.

### Training settings and metadata

`TrainingConfig` in `python/train_nn.py` owns batch size, seed and learning rate;
both the plain script and `/train` use `create_training`. Change hidden widths
or depth in `AuraPolicy`'s `hidden_sizes` default in `python/aura_policy.py`.
The input width comes from the environment, and the output width comes from the
policy's action transforms. Sampling scale and action transforms also belong to
the policy. Restart `/train` after changing these settings.

The worker reports the actual batch size, environment episode limit, layers,
parameter tensors, device, exploration scale and action transforms. Tables,
formulas and plot batch spans use these values; larger batches/models scroll.
The displayed loss describes the implemented reward-to-go objective with a
leave-one-out baseline at each timestep,
and optimizer equations describe the current Adam training algorithm.

The C++ action contract in `include/aura/sim/ActionSpec.hpp` owns command names,
labels and allowed joint limits. Physics and the control CLI use it directly;
Python receives it through `aura_python.action_specs`. The environment obtains
its episode limit from the simulator rather than duplicating it in Python.

### Movement and joint controls

`/control` first runs `python3 -m python.control_aura --prepare`, showing the
controller's build and launch output. The Python controller configures and builds
AURA and its Python bindings, sets neutral targets, and opens the viewer. Only after successful startup
does the CLI open Joint vectors: one row per joint with bend, turn and tilt targets.
Tab selects an available axis; Left/Right adjusts it; Enter accepts one angle for the selected axis or a value for every
available axis.
M opens optional movements; J returns to Joints; Enter edits the selected component. Failed or interrupted startup
returns to the prompt. Startup output remains available in the CLI transcript.

On terminals with enough room for the joint table and preview, the right panel shows a rotating
dotted schematic using the live viewer’s part transforms. The rotation is a
preview camera orbit. Observation feedback shows pelvis height,
uprightness, BALANCED / UNBALANCED / FALLEN state, data freshness, simulation time, steps, speed,
spin, and pause state. Status cards summarize the body, and the header reports
LIVE, WAITING, or STALE. Smaller terminals keep the compact joint editor.

The control screen temporarily replaces the chat viewport and restores your
transcript when you leave. Joint vectors show grouped targets and selected-axis
limits and measurements. Its selected-joint panel shows actual angles, targets, errors and limits for every
component. Each available axis has a smooth range track bounded by its numerical
limits: a green actual marker, purple target marker, and a combined marker when
both share a display cell. Missing/stale telemetry hides the actual marker;
measurements outside the limits use an outward-pointing end bracket. Short
terminals retain all three axis tracks. The header shows live body status. Viewer status changes from waiting to live,
then stale if its
observation file stops updating.

In the **Joints** tab, Up/Down selects among all 15 joints. Tab selects an
available axis; Left/Right changes it by the chosen step. S cycles through 0.01,
0.05 and 0.20 radians. Enter opens the angle editor: one value changes the selected component;
multiple values update the available axes in bend, turn, tilt order. R requests neutral for all targets. Esc cancels an
unfinished edit;
Q goes back and Esc returns home when not editing.

The selected-joint panel shows all three components with their actual angles,
targets, errors and limits. Missing or stale measurements show a dash; unsupported
axes are locked at zero. Motor-off, clamped-target and out-of-limit warnings are
shown there too. The separate head motor remains disabled.

Each successful edit atomically updates `viewer_action.json` in the AURA project.
The running viewer reads those targets. Component limits come from the shared
simulator metadata; hip bend is -1.8 through 0.5 radians and knee bend 0.0 through 2.2.
Invalid numbers are rejected, and file errors leave the last applied values intact.
This screen does not accumulate tables or editing messages in the chat transcript.

Without full-screen terminal support, the fallback accepts `row bend turn tilt`,
for example `1 -0.3 0.2 0.1`. Use `m` for movement groups with the same angle syntax,
`j` for Joints, `reset` for neutral, and `q` to return. A scalar edits bend in this
line-based fallback; tuples select the group's available axes in order.

## Editing and scrolling

- Tab sends the draft when no menu is open; Enter also sends.
  Empty or whitespace-only drafts are ignored without adding transcript entries.
- While a shell command runs, type the next input and press Tab or Enter to queue it.
  Inputs run sequentially after the active command finishes. The footer shows the
  queue count. An unfinished draft is preserved across command completion.
  Ctrl+C stops the active command and clears waiting inputs.
- Up/Down navigate menus; outside menus they recall previous/next inputs.
  Edits to recalled inputs and the unfinished draft are preserved while browsing.
- Ctrl+P / Ctrl+N are aliases for input history.
- Ctrl+R searches previous inputs; type to filter, Ctrl+R again for older matches,
  Enter to recall for editing, or Esc / Ctrl+G to restore the original draft.
  Search works at the idle prompt and never executes a match itself.
- Page Up / Page Down scroll output by a page.
- Left/Right and Ctrl+B/F move the cursor; Home/End and Ctrl+A/E move to the
  start/end. Alt+B/F moves by words (Esc then B/F also works).
- Delete erases ahead; Ctrl+U/K erase before/after the cursor; Ctrl+W erases a word.
  Ctrl+Y restores the last text removed with Ctrl+U/K/W.
- Ctrl+C cancels a draft or stops a running command; Ctrl+D exits on an empty draft.
- Ctrl+L clears the transcript. Escape dismisses menus.
- Cmd+V pastes into the draft without submitting; line breaks and indentation are preserved.
- Drag selects terminal text; Cmd+C copies the selection. Selection uses a
  soft lavender highlight with dark charcoal text in iTerm2, including over gray input boxes.

Long drafts wrap upward into at most six lines. The prompt stays at the bottom
while output is browsed. Drag normally to select text and use the terminal's
Copy shortcut.

### iTerm2: native selection and separate wheel scrolling

Run `./cli/install.sh --iterm2`, then open a new **Profiles > Aura CLI** session.
The dedicated profile uses `AURA_MOUSE=wheel`, reports wheel events, and leaves
clicks and drags to iTerm2's native text selection. Drag to select, Cmd+C to copy,
and Cmd+V to paste without submitting. Ctrl+O toggles the latest output details;
the command center provides access to earlier runs. Physical Up/Down edits history.
The existing appearance and Shift+Enter mapping are preserved.

Ordinary iTerm2 launches request the same wheel-only session settings. CLion/JediTerm and other terminals also enable
real wheel reports by default,
preventing wheel-to-arrow history recall. CLion requires Shift-drag for native
selection while mouse reporting is active; iTerm2 supports plain drag with its
separate click-reporting setting. Apple Terminal uses native selection and
PgUp/PgDn instead because it lacks mouse reporting.
`AURA_MOUSE=click` opts into clickable welcome actions and Details, requiring
Option-drag in iTerm2 for native selection. `AURA_MOUSE=off` disables reporting.
Global iTerm2 preferences are not modified; the saved profile is restored on exit
when `ITERM_PROFILE` is available.

The dedicated profile is saved at
`~/Library/Application Support/iTerm2/DynamicProfiles/aura-cli.json`; removing
that file removes the profile.

Shortcut conventions follow the
official [GNU Readline history](https://www.gnu.org/software/bash/manual/html_node/Commands-For-History.html),
[movement](https://www.gnu.org/software/bash/manual/html_node/Commands-For-Moving.html),
and [killing/yanking](https://www.gnu.org/software/bash/manual/html_node/Commands-For-Killing.html)
documentation. Wheel/selection behavior
follows [iTerm2 Terminal preferences](https://iterm2.com/documentation-preferences-profiles-terminal.html)
and [Dynamic Profiles](https://iterm2.com/documentation-dynamic-profiles.html).

### Useful local commands

- `!git status --short --branch`: show changed files and branch information.
- `!git diff --stat`: summarize unstaged changes by file.
- `/cd <directory>`: change the CLI workspace for subsequent commands.
- `@README.md`: preview a file; `@` opens the file browser.

Git examples follow the official [status](https://git-scm.com/docs/git-status)
and [diff](https://git-scm.com/docs/git-diff) documentation.

## Compatibility

Screen controls and styles come from terminfo. Small panes omit chrome to keep
the input visible, and resizing invalidates the old frame before repainting.
UTF-8 locales use Unicode rules/prompts; other locales use ASCII.

Use `AURA_ASCII=1 aura-cli` to force ASCII, or `AURA_TUI=plain aura-cli` for a
line-oriented session. Unknown terminals, missing TERM, TERM=dumb, and redirected
input/output also use plain mode. Plain mode has ordinary terminal scrollback
rather than a fixed composer. Session history is held in memory and is not saved.

## Uninstall

```sh
rm ~/.local/bin/aura-cli
```

The source stays in `cli/`.

## Interface layout

The welcome header is drawn from the current terminal size, rather than saved as
fixed-width transcript text. After the first submission it gives way to a small
persistent title and AURA status line. `/clear` restores the welcome screen.
Menus have a title, description, aligned labels, and a highlighted selection.
The `?` panel groups shortcuts under Compose, Session, and Output; smaller panes
show fewer columns and offer the full text guide on Enter.

The interactive queue holds up to 32 inputs. Drafts composed while a command is
running are limited to 64 KiB; bracketed paste stays a draft until submitted.

Typing `!` switches the composer to shell mode: `!` replaces the regular prompt
marker. Both normal and shell input use a gray background on color-capable terminals.
The prefix remains part of the submitted command but is displayed only once.
Backspace on an empty shell draft removes the prefix and restores normal input.
`NO_COLOR=1` disables the background and marker color.

## Command center

Press Left Arrow on an empty prompt to open the command center. It starts with **Models**: all project checkpoints,
their sizes, update dates and compatibility. The selected model also shows saved
episode/update counts and its stored best score when available.
Use Up/Down to select, Enter to inspect, W to watch actors, E to evaluate, T to resume training, and R
to refresh. Incompatible checkpoints remain visible but cannot be run. These actions
reuse the same Release build and workers as the slash commands. Model checks show
an immediate loading acknowledgement and remain cancellable with Q or Esc; the
loading screen also updates when the terminal is resized.

Press 2 for **Experiments**, 3 for **Designs**, or 1 to return to the model library.
These tabs list saved items without leaving the command center. Action errors appear
in the current tab; command output remains available in the transcript and Ctrl+O view.
Q returns to the prompt; Esc returns directly home. During name/value editing, Esc cancels the edit first and Q
remains ordinary text. Applied targets and the last saved checkpoint are retained.
With a nonempty draft, Left Arrow moves the cursor normally. Native text
selection uses normal dragging in the default profile.

Each shell run records its command, working directory, exit code, elapsed time,
and finish time. Opening a run does not execute it again. The center is local to
the current process; `/clear` clears its completed runs and their output. Queued
inputs remain visible until executed or cancelled. Open the center after the
active command finishes.

The control and neural dashboards report the active motor configuration. The head
motor is currently disabled by default; its target is visible but is not applied.
The model picker checks observation and action contracts before resume, playback,
or evaluation. Historical incompatible files remain visible and unchanged.
Compatibility checks both the neural interface and reward/termination contract; it does not measure model performance. Evaluation history
is compared only when the observation, action, and motor configurations match.

In the training Overview, `T` expands or collapses the latest joint targets.
Targets start collapsed to leave room for the survival and loss graphs.
The best-balance card keeps a compact height independently of target details.

### Paired critic-baseline gradient diagnostic (Step 111)

This is an explicit historical legacy experiment, separate from current actor training and the Critic Lab:

```sh
PYTHONPATH=build-release:. python3 -m python.experiments.gradient_experiment --output docs/gradient-step111-new.json
```

Choose a new output name; existing reports and checkpoints are never replaced.
The experiment reconstructs Step 110's frozen seed-42 actor and seed-43 critic (32 training episodes, 16 validation
episodes, exploration seeds 4201/4202,
20 critic updates). It then freezes the critic and collects three fresh
32-episode batches using exploration seeds 4211/4212/4213.

Within each batch, both losses use the **same sampled-action log probabilities**,
state snapshots, and discounted returns. The original leave-one-out baseline is
unchanged. The critic predicts `G/100`; its alternative advantage is
`(G/100 - V(s)) * 100/episode_limit`, keeping the actor's original return scale.
Both losses still sum timestep terms and divide by episode count. Gradients are
measured before Adam; there are no actor optimizer steps and no critic updates
during this comparison. The report includes gradient norms, pairwise cosine
similarities, seeds, critic reconstruction results, and parameter-integrity checks.

These new paired measurements are the fair comparison; historical cosine values
may use different exploration streams. Three batches do not establish gradient
variance reduction or improved balance, even if their cosine agreement increases.

Typing, editing, pasting, and recalling history preserve the visible transcript
position while scrolled up. Suggestion menus and multiline drafts keep the top
visible transcript row anchored. Esc at the main prompt dismisses menus and jumps to the latest output, retaining
the current draft. Page Down returns toward the latest output;
submitting a command or clearing the screen starts at the latest output.

The Critic Lab displays animated waiting indicators before MSE measurements exist,
with the current collection phase, episode counts, elapsed time, and time since
last reported progress. After 30 seconds without a new progress report it shows
a quiet-progress notice and cancellation keys; this is not a timeout or proof
that the worker is stuck. Error curves replace placeholders once measured.
Stopped/failed runs show an explicit outcome instead of continuing to animate.

`quit` and `/quit` are exit aliases. Typing their prefixes offers the canonical
`exit` or `/exit` completion with “Close Aura CLI,” without adding a second
command to the menu. Enter executes the same exit action.

### Three-axis manual shoulders

Press **M** in `/control` for **Movement vectors**: body-part groups with the
same **Bend / Turn / Tilt** angles, editing rules, scales and limits as Joints.
Both arms and both legs link the left/right shoulder or hip. Both hands link
wrists; both feet link ankles. Torso uses the waist and Head uses the neck.
Individual left/right groups are available too. Child joint targets remain
independent: moving a shoulder preserves the elbow and wrist targets.
Paired bend values match, while turn and tilt are mirrored across the body. **Mixed** means the two sides currently
differ; a scalar edit links just the
selected axis. Paired scales show the left side. Press **J** to inspect an
individual joint or **M** to return to the movement groups.
Joints groups 33 coordinates across 15 physical joints. Shoulder **bend** raises
arms forward with negative values (−1.57 straight ahead, −3 nearly overhead).
Shoulder **tilt** lifts sideways: left negative, right positive (about 1.57 radians
for horizontal). With arms forward, shoulder **turn** moves them inward: left
positive, right negative. Use Tab to choose a component, Left/Right to adjust it,
Enter to edit one component or the joint together. R requests neutral. These are desired motor targets: balance,
collisions and other joints affect the achieved pose.

The actor now has 32 outputs for all enabled joint coordinates. The critic keeps
205 inputs and one value output; its quality must be re-evaluated on this body.
Older trained policies remain on disk and appear incompatible with the revised
shoulder body. Start a fresh model for this body; keep historical results with the
matching historical code.

Best actor checkpoints (`_best.pt`) are read-only: Watch, Evaluate and frozen-actor
critic experiments are available; Resume and Restart use the latest checkpoint.
New actor model names cannot end in `_best`.

The model library includes actor checkpoints and saved critics. Actor/critic labels and
older-body warnings stay in the library. Search accepts names, roles and body labels.
Press **Enter** for a separate, scrollable network sheet: saved architecture, observation
groups and numbered movement outputs. **Q** returns to the library; **Esc** returns home.
Layer cards show the saved dimensions and activations. Wide terminals place the
observation table beside the output panel; narrow terminals stack them. Critic error
figures use readable precision and compare with the saved constant baseline.
Enter opens the network inspection sheet for actors and critics. W watches actors; critics are inspect-only.
Watch/Evaluate/Resume remain actor actions.
Trajectory archives are excluded. No network-detail panels are added to `/train`.

`/control` opens **Joint vectors** by default: one row per physical joint, with **(bend, turn, tilt)** targets in
radians. Up/Down chooses the joint, Tab chooses
its next available component, and Left/Right adjusts that component. Enter
accepts one angle for the selected axis, or one angle per available axis, in bend,
turn, tilt order. For a shoulder, `0.2 0.2 0.2` sets all three axes; for an ankle,
`0.2 0.1` sets bend and tilt, skipping the locked turn axis. A knee needs one value.
Commas and optional parentheses also work. Explicit three-component tuples remain
supported on joints with locked axes, which must be zero in that form. S changes the adjustment step, R requests
neutral,
Q returns, and Esc returns home (or cancels an unfinished edit).

These are **joint coordinates, not world XYZ or ordinary Euler angles**. Bend is
signed rotation about the existing hinge axis (the elbows use local -X); turn and
tilt are the existing parent-local Y/Z swing components. Each component retains
its limits; the solver still determines the reachable pose under contacts and
coupled constraints. The separate head motor remains disabled.

M opens grouped movement vectors; J returns to Joints. Enter edits a selected
axis in either tab; there is no separate single-angle tab. Redirected input accepts `row bend turn tilt`.
`ControlVectors.hpp` derives the groups from the shared action metadata and
provides `JointTargetVector`, `joint_target_vector` and `with_joint_target` for
readable tuple-based editing. The actor's 32 outputs, observation encoding, flat
action-file format and physical solver are unchanged.

Both `/control` tabs show body state, motor counts, and telemetry freshness. The
wide body panel includes simulation time, steps, speed, spin, and sample age above
the live pose. Stale samples are labeled and do not supply tracking measurements.
Movement groups report the mean absolute tracking error across their participating
joint coordinates and flag disabled motors. This is an angle error, not a balance
score. Both tabs retain per-axis scales, numeric targets, measurements and limits.
# Primary Balance System

New `/train` sessions use the whole-body classifier: BALANCED and UNBALANCED
continue, FALLEN ends the episode, and 1200 steps truncate it. Each nonterminal
transition earns the continuous balance-quality score (0–1); FALLEN earns zero.
There are no Reward V2 penalties or absorbing negative failure utilities.

New sessions initialize a fresh actor and critic, with separate Adam optimizers,
and save them as a paired checkpoint.
Resume restores both networks, both optimizers, exploration RNG, and the exact
environment contract. Legacy checkpoints remain available for inspection and
explicit historical replay; incompatible resume/restart actions are disabled.
Use **New model** to begin the new objective. The observation/action interfaces
remain 205 inputs and 32 motor targets.

The Learning page reports training, deterministic, and stochastic balance metrics.
Evaluation reports balanced duration, unbalanced steps, longest balanced interval,
recoveries, average quality, total reward, fall rate, and termination reason.
Initial landing does not count as recovery. Best checkpoints rank deterministic
balanced steps first, then total quality reward; survival is still displayed as
a separate diagnostic. One evaluation episode per mode is a smoke measurement,
not a statistical estimate of robust balancing.

The home screen identifies paired actor–critic learning, `/control` displays the
classifier's physical state, and model inspection identifies the saved objective.
Standalone critic-only experiments use the new reward but do not automatically
replace the paired training critic. Existing CLI sessions can load the rebuilt
CLI with `/update`.

`/status` and `/help` describe the active Balance System, including its contract
versions, zero terminal reward, and balanced-duration best selection. Training and evaluation results
retain balance summaries and scaled critic MSE. Before a critic update is measured,
the training display shows MSE as pending. The compact best-policy summary and
model library show saved balanced steps and duration; the latest model can display
the metrics stored in its matching best copy. Older survival-only scores are
explicitly marked diagnostic. Standalone critic compatibility checks the saved
objective as well as the neural interface, and incompatible actors are described
as inspect-only rather than resumable. These displays do not modify checkpoints
or change training behavior.

Historical arena overview supports **Left/Right** to select an arena agent. For current independent sessions, select a worker instead. Expanded joint
targets belong to that selected agent and show commanded radians, not measured
angles. Learning shows completed-episode means for each agent in the current batch,
with return range, balanced duration, recoveries and fall rate. Its charts retain
the last 120 completed episodes across updates. Spawn spacing is saved in the
environment contract: new arenas use 12 m; resumes and replays keep saved spacing.
The arena camera looks diagonally down from a corner, framing the occupied area.

Episode Diagnostics has a bounded list with its own scrollbar and row range.
In Learning details, Up/Down, Page Up/Down, Home and End scroll episodes while
the list is focused. Tab switches between episode scrolling and page scrolling.
The mouse wheel scrolls the list when over its panel, or the page outside it.

## Ground contact and viewer termination

The current ground is dry and grippy: static coefficient 0.8, sliding coefficient
0.6. Friction is limited by each contact's normal support, including support in the
final room pass, and works for every contacting body part. Internal joint and
self-collision projections preserve horizontal whole-body centre of mass when room
constraints permit. The normal solver retracts excess support impulses to reduce
artificial bounce at zero restitution. No global horizontal velocity clamp is used.

The normal `/run` window continues physics and accepts controls after FALLEN.
Space pauses/resumes, R restarts and P resets to a paused initial pose.
Model playback, evaluation and training retain their episode termination
condition. Reopen the standalone window to load a newly built physics executable.

These dynamics use environment contract 5, quality version 2, and ground version 3.
A checkpoint from an earlier ground/projection contract is preserved but cannot
resume silently under the new physics. `/status` reports the current contract and
ground coefficients. Reward, input/output dimensions and network architecture are
unchanged by this ground correction.


## Compare, diagnose and report

`/compare` selects an actor and compares frozen initial, best and latest policies.
It uses CPU evaluation in the saved solo/arena world, one deterministic evaluation
and three matched exploration seeds (190042–190044) per policy. Balanced duration
is primary; survival and reward provide context. Newly created CLI models retain
the exact initial actor. Older files use an explicitly labeled seed reconstruction
when possible. Missing best files and different training runs are identified, never
silently substituted. CPU and MPS numerical results can differ slightly.

`/diagnose` (`/diagnoise` is accepted) checks actor/critic finiteness, optimizer
steps, environment compatibility, recorded history and one bounded deterministic
rollout. This is a health check, not proof of learning or critic prediction accuracy.
`/report` exports existing saved metrics without launching evaluation. Reports can
also be exported for archived actors that cannot run in the current environment.

All three open on a compact colored **Overview**. Comparisons use a side-by-side
policy table; diagnoses show model health and balance results; reports show saved
metrics and exports. **2** opens full **Details**, **1** returns to Overview, and
**D** or **Tab** toggles between them. **Up/Down**,
**Page Up/Down** and the wheel scroll results. **Q** returns; **Esc** goes home.
Comparison and diagnosis can be cancelled safely. Each completed command writes
new Markdown, JSON and CSV files under `reports/`, including a snapshot hash and
individual results. They never overwrite checkpoints, training locks or the
Command Center evaluation cache. A live worker may advance while they inspect its
last complete saved checkpoint.

New CLI saves include `training_history_version=1`, per-update actor/critic loss,
actor gradient norm, maximum parameter change, episode measurements, deterministic
and stochastic balance metrics. Full scalar history remains in checkpoints; charts
show a recent window and restore it on resume. Missing past history is reported as
unavailable. No checkpoint/environment contract version changes are needed because
these fields add metadata without changing rewards, physics or learning.

The Overview opens on training and evaluation **balanced time**. **G** cycles
balance, survival and policy loss. On a live training screen, the first **Q/Esc**
opens a confirmation; the next **Q/Esc** exits, while **N** keeps training. Training
continues behind the confirmation until exit is confirmed. Saved updates remain
intact; an unfinished update is discarded. Ctrl+C remains an immediate interrupt.

An already running CLI and Python worker keep their loaded code. Let the current
training finish, then use `/update` from home to reload the CLI. History recording
starts when the updated worker is next launched; missing metrics from an older
running worker cannot be recovered from weight-only checkpoints. NumPy is optional
for the tensor-only trainer; if missing, the warning names the actual active device
and cannot mask a training failure. Install it in the CLI's Python environment with
`python3 -m pip install numpy`.

### Reward repair and saved actors (quality version 3)

New models use physical motion costs in every classifier state. Small COM/foot
corrections remain inside deadbands; a lifted foot is not treated as slipping.
Rotation quality is continuous, calibrated to the existing classifier control
speeds; neither classifier thresholds nor physics have changed.

Models trained on quality version 2 are shown as **historical**, rather than
silently resumed under a new objective. Watch, Evaluate, Compare and Diagnose
use their saved reward version. In `/train`, choose **5 Fork actor**, select a
saved actor (the best copy is allowed), and choose a new name. This copies its
exact actor weights, retains the saved world/agent count, and initializes a
fresh critic and both Adam optimizers. Episode/update counts restart at zero.
The source and its best copy remain untouched; the new checkpoint records the
source filename and hash. Do not resume the final regressed actor merely to
accumulate more episodes. Start with a short comparison from the saved best.

Reward changes fix objective mismatches; they do not establish that training
will converge. Evaluate balanced duration and recovery, not survival alone.


## Model designs and experiments

Use `/create` to open the model builder directly and save an untrained design.
Use `/experiment` (alias `/experiments`) for experiment setup, runs and results.
Saved designs and experiments are separate libraries in `/center` →
**3 Designs** and **2 Experiments**, respectively.
N creates; Enter inspects; C copies into a new draft. Disabled actions are gray
and their shortcuts do nothing. Q/Esc returns without affecting a training model.

**F2 Rename** changes an experiment's name directly in the library or workspace,
including completed runs. Enter saves; Esc cancels. Its folder, settings,
checkpoints, audits and results stay attached. Editing completed run settings
still creates a separate draft so the original results remain reproducible.

Enter opens one experiment workspace with fixed **1 Setup**, **2 Training**,
**3 Audit**, **4 Compare**, and **5 Runs** tabs. Setup uses Tab to switch setting
groups; E edits drafts or creates a linked copy of a completed run. D selects a
saved network design, starting fresh weights. In the editor, Ctrl+N/Ctrl+P
(or F3/F4) change groups; Tab switches fields. Training uses H to switch between summary and saved
per-update balance/critic accuracy. Compare shows grouped measured bars.

**A Audit actors** discovers all trained checkpoints from the current training
run, freezes their weights, and audits them with matched rollout seeds. No paths
need to be entered. The Audit tab shows reward-component bars; L selects an older
audit. Runs lists the original, linked copies, and linked audits. Edited copies
live in `<experiment>/runs/`; audits live in `<run>/audits/`. Existing schema-1
experiments require no migration; their saved configuration, results and weights
are preserved. Parent execution locks protect a workspace while a child runs.

Experiment and builder inputs use fixed columns across the available width.
Wide terminals show grouped settings alongside a live actor/critic preview.
The diagram shows layer widths, hidden/output activations and parameter counts,
with run settings beneath it. Invalid or incomplete widths are identified without
building a network. Smaller terminals use explicit PgUp/PgDn pages instead of
scrolling the input list with each Tab press. Nested layer/parameter editors use
the same fixed-column layout.

A design stores architecture and initialization, **no learned weights**. Give it
a name and choose actor/critic hidden widths, activations, and initialization.
Hidden-width and seed lists accept `64 64 64`, `64,64,64`, or `[64,64,64]`.
F2 on a hidden-width field opens individual labeled layers; A adds and D
removes a layer. Supported local budgets are 0–8 hidden layers and 1–2048 neurons
per layer. Activations are Tanh, ReLU, Sigmoid and Identity; initialization is
PyTorch default, orthogonal or Xavier uniform. The actor output stays Tanh and
the critic output stays a scalar Linear layer. Inputs/actions stay 205/32.
T on a saved design creates an experiment using it. Saving a design never trains it.

Experiments support frozen baselines, actor–critic training comparisons,
frozen-actor critic fitting, physical reward-component audits, and worker
throughput benchmarks. Each type exposes applicable settings from a shared
registry. Select one comparison field and values, retain fixed settings, set
training and evaluation seeds, review the budget and output path, and save a
draft. R reviews and runs that draft. P pauses; Q/Esc twice cancels, N continues.
Completed/interrupted runs are immutable; C creates another experiment.

Training methods are production MC actor–critic and experimental clipped PPO.
Reward alternatives are product, geometric mean, arithmetic mean and minimum,
with validated quality parameters. These are named schema-1 experimental
formulas, not arbitrary executable expressions. Termination remains independent.
Training and critic-fit studies start a fresh critic and optimizers; optional actor transfer
must match its architecture and physical interface. Source checkpoint bytes are
snapshotted and hashed, and source weights remain untouched. Every-step component
means come from actual simulated episodes, not invented reward trajectories.

Results include deterministic and stochastic evaluations, per-update history,
assigned episode seeds, worker identity, environment contract, source/build hashes,
checkpoints, paired control differences, and completion/error state under
`models/experiments/<unique-run>/`. Critic-only studies also report held-out critic
accuracy. Frozen baselines using one saved actor cannot count duplicate training
seeds as independently initialized policies. Evaluation seeds are checked against
actual derived training episode assignments.

Promotion is explicit: P chooses a branch, reviews predeclared minimum balanced
time, maximum fall rate and seed count, then validates every seed. Diagnostic
step limits, throughput/reward-only studies, critic-only studies and altered reward
objectives cannot become the production default template. Promotion uses an
architecture with **fresh weights**, not a trained checkpoint, and records its
criteria. New CLI sessions honor the explicit promoted default. A particular
saved design can also initialize a fresh run with the backend `--template` option.
No unseen-posture or learning-success claim follows merely from completion.

Only the existing neutral reset is supported. Recovery counts are measured;
recovery time and configurable starting tilts are not implemented. Existing
research scripts remain available for historical or specialized studies.


### Saved lists and recoverable deletion

The three `/center` tabs stay inside the command center. Experiments and Designs
show saved records and their details; they do not open experiment setup. Enter
expands the selected record, Up/Down scrolls its details, and Q returns to the
list. R refreshes. All tabs share the same four-cell spacing.

The muted-red **[D]** key appears last; its **Delete** label stays gray. It names
the selected checkpoint, design or experiment and asks for confirmation. It moves the selected item into `models/.trash/`; it never silently
deletes a best copy or another model. Deleting an experiment moves its entire
folder, including results and output checkpoints. **U Restore** restores the
most recent deletion without overwriting an existing item. Active training and
experiment locks prevent removal; the promoted default design is protected.
Delete and restore are also available in the saved `/experiment`/design library;
**C Copy** duplicates a configuration there, keeping D consistent for deletion.

Shortcut footers share the dashboard layout: muted horizontal separator, whole-key
actions packed into bottom rows, and one final row for status. Center and saved
experiment/design libraries wrap controls at narrow widths without lifting them
several rows above the bottom. Navigation tabs remain beside their content.

Full-screen experiment and model-builder interfaces borrow AURA's active screen
without switching buffers. Returning keeps the shell hidden, and exiting the CLI
restores the shell without leaving dashboard residue.

Experiment dialogs show their own confirmation and exit controls, independent of
workspace tabs. N resumes a paused experiment; Q/Esc twice cancels it. Running
screens use nonblocking keyboard polling and show preparation, evaluation,
collection, and audit stages. Ctrl+S cannot suspend terminal output; terminal
settings are restored on exit. Large saved histories are reparsed only when their
evidence files change. Long lists and tables stay above the shortcut separator.

The CLI does not use NumPy interoperability and suppresses only PyTorch's optional
missing-NumPy import warning. Other runtime and accelerator warnings remain visible.

Temporary success confirmations (save, delete, restore, apply) disappear after five
seconds without requiring a keypress. Errors remain available until the next action.
Read-only screens hide the terminal cursor; the command prompt shows it for typing.

Experiment runs use a progress bar with ETA, physical metric cards, recent
evaluations, and a network/settings sidebar when space permits. Saved results
open on **1 Overview**; **2 Settings** keeps configuration separate and
**3 Comparisons** shows paired stochastic candidate/control differences.
**Tab** changes the branch summarized by the cards; **M** switches saved
stochastic and deterministic evaluation. Result rows retain measured before/after
values, and physical promotion criteria are reported without automatic promotion.

Embedded curses interfaces use the system `tic` tool to create a temporary
terminal description with screen-buffer entry/exit disabled. Colors and keys
inherit the original terminal; the description is removed when the interface ends.
Standalone launches retain their normal curses screen lifecycle.

CLI updates transfer ownership of the active screen to the replacement process;
failed reloads return to the existing build on the same screen.

Worker balance and survival charts plot raw episodes with finer connected lines,
use a common worker-local episode index, and scale only the visible 60-episode
window. Legends show each worker and its latest measured value; * marks the
selected worker. Left/Right selects a worker and H isolates its curve. Overlapping
terminal cells prioritize that selected trace. No smoothing changes the samples.
