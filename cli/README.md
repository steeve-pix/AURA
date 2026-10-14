# Aura CLI

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

`/train` starts with four choices: **Resume**, **New model**, **Restart**, or **Critic only**.
Resume loads a selected model's network, Adam state, saved sampling RNG state and
cumulative episode/update counts. New model asks for a name and starts fresh;
names are case-insensitive and receive a numeric suffix when already used (including
another active training session). Restart asks for confirmation, resets learning,
and replaces the selected latest and best checkpoints with the fresh starting state.
The selected name and saved episode count remain visible in the training workspace.
One model can be trained by only one CLI session at a time.

Training saves the selected checkpoint in `models/` after each completed batch.
This is the latest state, not a verified best model. It contains
weights, biases, Adam state, the actual network layout, action settings, input
size and the cumulative number of completed episodes. Saving replaces
the previous latest checkpoint atomically; an interrupted batch leaves it intact.
The standalone `python/train_nn.py` experiment still starts fresh and writes
`models/aura_balance.pt`; use the CLI for model selection and resumed training.
Project builds use `build-release` with CMake's Release configuration for the
simulator and Python extension, so CLI training uses the optimized simulation.

Open the **Command Center** with Left Arrow on an empty prompt or `/center`.
Its Model library is the default tab: checkpoints on the left, compatibility,
architecture, saved episode/update counts, best score and modified date on the
right, and recent runs for the selected model below. This uses the same open,
full-width layout as Model logs. Narrow panes keep the model list and session log.

Each model’s latest checkpoint and its `_best.pt` copy appear together, with
**Latest** and green **Best** labels. Unpaired regular checkpoints are **Saved**;
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

Arrow keys select a model; **Enter** watches, **E** evaluates, **T** resumes
training, and **R** refreshes the library. **2** opens current-session model events
and their details; **1** returns to the library. Logs are session-only; saved
checkpoint metadata and evaluation history remain on disk. Model commands are
removed from the command menu. Historical `/models`, `/model`, `/evaluate`, and
`/eval` inputs redirect to the Command Center. To retain a separate snapshot
before more training, copy the checkpoint to another `.pt` name in `models/`.

Playback reconstructs the saved network, checks input size and action keys against
the simulator, and uses the policy's mean action without exploration noise or
weight updates. It runs one episode at the simulator's normal pace, then holds the
final pose. Close the viewer or press Esc in its window to return to the library.
Older checkpoints with an unsupported format or incompatible input/action interface
are rejected with a message; changing default hidden-layer sizes does not invalidate
a checkpoint written in this format.

Select a model in the Command Center and press **E** to measure survival in one
headless episode without exploration or weight updates. The results screen shows
survival steps, the previous comparable result for that model, and the change.
Q stops evaluation and returns to its parent screen; Esc stops it and returns
directly to the main prompt.
Measurements are stored separately in `models/.evaluations/`; checkpoint files
are unchanged. Comparisons reset if the simulator, observation encoding or
evaluation method changes. This is a measurement from the normal reset pose,
not a claim of general balance stability.

`/train` first evaluates and saves the fresh starting policy as a best-model
candidate, before any optimizer update. It then evaluates the updated policy
after every batch is saved. Its
Overview shows training Batch Mean and Best Episode alongside Eval Survival
(no exploration noise), Optimizer and Policy Loss. The evaluation card identifies
the tested update and its change from the previous evaluation in this session.
After evaluation, a strictly better score atomically promotes the exact saved
state to `<model>_best.pt`; the Overview shows its best evaluation score and file.
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
The best-evaluation card also shows the current score's gap from the saved best.
Model pickers, setup and evaluation share the same header and bottom navigation;
selection highlights span the full row, and long model names end with an ellipsis.
Successful built-in builds show a compact named summary. **Ctrl+O** opens their
full command and build output; failed commands retain visible error guidance.

## Install and launch

Run `./cli/install.sh`, then type `aura-cli` from any directory.
The standalone executable is installed at `~/.local/bin/aura-cli`.
Re-run the installer after source changes. The installer only builds the CLI;
`aura` and Aura.app belong to the simulation and are separate.

Build manually with:

```sh
cmake -S cli -B cli/build
cmake --build cli/build --target aura_cli
./cli/build/aura-cli
```

Requires CMake, a C++20 compiler, and system curses/terminfo. macOS includes the
library; Linux builds need the distribution's curses development package.

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

| Input | Action |
| --- | --- |
| `/` | Browse local commands; type to filter, Tab to complete, Enter to run. |
| `!command` | Execute a real shell command in the current directory. |
| `@` | Browse files; Left/Right switch All, Files, and Directories filters. |
| `@path` | Preview a text file, or list a directory. Paths with spaces can be quoted. |
| `?` | Show keyboard shortcuts and input modes. |

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
muted body. Empty output explicitly shows `(no output)`. Completed shell output previews its first three wrapped lines in gray; while running,
the latest six lines remain visible. Collapsed command headings fit one line with
`…` when needed. Expanding Details restores the complete command and readable
output in the normal text color. Longer output is followed by `+ Show details` and the
number of hidden lines. Ctrl+O toggles the latest entry; optional click mode also allows clicking Show details / Show less.
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

| Command | Action |
| --- | --- |
| `run` | Configure and build the existing `aura` target, then open the simulation viewer. |
| `build` | Configure and build the simulation, without launching it. |
| `control` | Open the viewer and the whole-body joint control table. |
| `train` | Build the Python bridge and open actor or critic training setup. |
| `center` | Open the model library; E evaluates the selected policy without exploration. |
| `status` | Report build artifacts, Python bridge availability, and the age of the last viewer observation. |

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

| Command | Action |
| --- | --- |
| `/help` | List commands and input modes. |
| `/shortcuts` | Show keyboard shortcuts. |
| `/voice` | On-device macOS dictation into an editable draft, with an audio level animation. |
| `/control` | Open a table to edit the 15 live joint targets. |
| `/train` | Train an actor or critic; inspect rewards, prediction errors, weights and evaluation. |
| `/status` | Show directory, runtime, shell, and model status. |
| `/files` | List files in the current directory. |
| `/cd <directory>` | Change workspace; quoted paths and `~` are supported. |
| `/history [clear]` | Show saved commands, or clear saved and current session history. |
| `/version` | Show the CLI version. |
| `/clear` | Clear the transcript and show the welcome area. |
| `/exit` | Close Aura CLI and restore the terminal. |

`/pwd` aliases `/status`; `/quit` aliases `/exit`. Commands also work as arguments,
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
center activity. Existing actor training and evaluation remain independent;
critic predictions are not yet used as actor advantages.

### Neural training workspace

`/train` replaces `/train_balance`. It builds the Python extension and opens
Neural Training Lab using the existing local PyTorch experiment in
`python/train_nn.py`. Dashboard controls and telemetry live separately in
`cli/nn_session.py`. The CLI observes environment calls through an adapter;
the training module contains no command handling or dashboard hooks. PyTorch must be installed in the `python3` environment;
the command reports import errors and does not install packages automatically.

By default each batch collects eight episodes and performs one Adam update. The
network uses 205 observations -> 64 -> 64 -> 14 motor outputs, with Normal
exploration (standard deviation 0.25), seed 42 and learning rate 0.001.
The head motor is disabled by default because its current gains oscillate when
the neck motor is also active. See the joint audit and supported configuration in
[the architecture documentation](../docs/architecture.md#neural-motor-contract-c-action-schema-2-policy-action-schema-3) before training.
The **Learning** tab opens a reward viewer. Its cards show the current episode's
accumulated return, average reward per survival step (with the last step's reward),
and the current batch's mean return across completed episodes. Live measurements
arrive alongside progress at the existing reporting rate; completed episodes are
always reported, including terminal and truncated episodes.

The reward-history graph shows total return by episode; wide terminals also show
average reward per step. History contains the last 120 completed episodes from the
current session, retaining absolute episode numbers after resume. It does not
invent historical rewards for episodes loaded from a checkpoint. Batch means use
all completed episodes of the current batch, even when the graph window advances.

The current reward is `0.5 + 0.5 * uprightness` on surviving steps, with uprightness
limited to `[0, 1]`; a terminated step receives zero. Higher total return can reflect
longer survival, so compare the average per step and deterministic evaluation too.
The panel explains this formula; no separate reward-component telemetry or critic
predictions are fabricated. **R** switches between this viewer and the existing
learning diagnostics and policy-gradient explanation. No reward, policy update,
physics, or checkpoint behavior changes.

The REINFORCE training objective gives each action its discounted reward-to-go
(discount factor 0.995). Its baseline is the mean future return at the same step
from other episodes that reached it, or zero when there are no peers. Step
advantages are normalized by the environment's episode limit; the loss sums
their log-probability terms and divides by the batch's episode count.
The scores and returns describe behavior before the update; decreasing loss alone
does not establish improved balance. Deterministic evaluation and best checkpoint
selection continue to measure survival steps.

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
  episode-level advantages and summed log probabilities. These episode advantages
  are diagnostics, not the step advantages used by the gradient. A connected
  four-stage learning cycle, reward-to-go objective and aligned layer
  equations. Raw Gaussian samples `u` enter the loss; clipped outputs `a` become
  joint targets. Adam and advantage fractions are centered with visible denominators.
  The most recent completed batch stays visible while the next one collects.

**Page Up/Down** or the **mouse wheel** scrolls any page. On Overview and Learning,
**Up/Down** also scrolls by one line. Smaller windows stack panels; a scrollbar indicates content below the viewport. Symbols, subscripts,
arrows and stacked fractions use terminal-width-aware Unicode. Adam uses a
standalone superscript c for bias-corrected moments, avoiding combining-accent
alignment differences in IDE terminals. `AURA_ASCII=1`
provides an ASCII fallback; `NO_COLOR=1` retains emphasis without colored text.

**P** pauses/resumes at a simulation-step boundary. After each batch, the screen
waits for review. **N** starts another configured batch with the same policy and
Adam state. **Q** stops the worker and returns to its parent screen; **Esc** stops it and
returns directly to the main prompt. **Ctrl+C** stops the worker.
Completed batches are saved to the selected model's checkpoint in `models/`.
**V** opens a live view of the same simulation; press **V** again in the terminal
or viewer to return to headless training. Esc or closing the viewer window also
returns to headless mode. Toggling preserves the current episode, policy, Adam
state and pause state. The viewer remains responsive while paused and between
batches. Drawing is limited to 30 frames per second without adding display pacing
to the training loop; rendering can still add overhead. Faster playback is future work.

Use at least 60 columns and 18 rows; 120 columns and 40 rows shows the complete
layout comfortably. Small terminals show a compact summary. Idle screens do not repaint
continuously. Noninteractive/redirected CLI runs collect one batch and print a
plain report. Direct runs also work:

```sh
PYTHONPATH=build-release python3 -m python.train_nn
```

Run `python3 tests/cli_nn_smoke.py` for real PTY integration coverage (two batches,
parameter inspection, pause/resume, scrolling, navigation, resizing and cancellation).
Run `ctest --test-dir cli/build --output-on-failure` for screen regressions covering
Unicode widths, malformed telemetry, stale snapshots and compact layouts.
The `terminal_shortcuts` test exercises the production prompt, joint controls,
training setup, training dashboard, evaluation and critic cancellation with
memory-only controls and a fake worker. It performs no learning updates and
writes no checkpoints. It checks both CSI and SS3 arrow encodings, editing,
pause/resume, visual commands, inspection, chart/reward toggles, and Q/Esc navigation.
The real model-library evaluation and filter tests run through
`tests/cli_command_center_smoke.py`; file filters, scrolling and immediate command
cancellation have separate `cli_file_filter_smoke.py`, `cli_scroll_smoke.py`, and
`cli_home_smoke.py` checks. Native clipboard and microphone behavior still depends
on terminal mappings and macOS permissions; these tests do not change either.
The older `tests/cli_balance_smoke.py` targets the retired command and is retained
as historical replay coverage, alongside `python/train_balance.py` and
`python/balance_session.py`.

Historical controller experiments live in `python/train_balance.py`; viewer pacing and
CLI progress reporting live in `python/balance_session.py`. Calling
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

### Joint controls

`/control` first runs `python3 -m python.control_aura --prepare`, showing the
controller's build and launch output. The Python controller configures and builds
AURA and its Python bindings, sets neutral targets, and opens the viewer. Only after successful startup
does the CLI open the dedicated control screen. Failed or interrupted startup
returns to the prompt. Startup output remains available in the CLI transcript.

On terminals with enough room for the joint table and preview, the right panel shows a rotating
dotted schematic using the live viewer’s part transforms. The rotation is a
preview camera orbit. Observation feedback shows pelvis height,
uprightness, standing status, data freshness, simulation time, steps, speed,
spin, and pause state. Status cards summarize the body, and the header reports
LIVE, WAITING, or STALE. Smaller terminals keep the compact joint editor.

The control screen temporarily replaces the chat viewport and restores your
transcript when you leave. It shows live pelvis height, uprightness, and standing
status. Each joint row shows the measured angle beside the commanded target and
its allowed range. Viewer status changes from waiting to live, then stale if its
observation file stops updating.

- ↑/↓ selects a joint.
- 1–9 selects the first nine joints directly. Arrow navigation reaches all 15;
  shorter tables follow the selected row.
- ←/→ changes its target by the selected step and applies immediately. Press S
  to cycle through 0.01, 0.05, and 0.20 radians.
- ↵ opens the selected value for editing; type an angle and press ↵ to apply.
  Esc cancels an unfinished edit.
- R sets all 15 targets to neutral. The head target is addressable, but its
  motor is disabled in the default configuration.
- The Error column shows target minus actual angle. Range tracks mark actual
  angles with ● and targets with ◆; ◈ means both occupy the same track cell.
  Missing or stale measurements show a dash. A gold ! marks a clamped target
  or an actual angle near/outside its limits. The selected-joint panel explains
  the warning and shows numerical limits.
- Q goes back; Esc returns home; Ctrl+C closes the controls. Applied targets stay active.


Each successful edit atomically updates `viewer_action.json` in the AURA project.
The running viewer reads those targets. Hips are limited to -0.8 through 0.8 radians
and knees to 0.0 through 2.2.
Invalid numbers are rejected, and file errors leave the last applied values intact.
This screen does not accumulate tables or editing messages in the chat transcript.

Without full-screen terminal support, the fallback accepts a row number and value,
for example `1 0.2`, plus `reset` and `q`.

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

Ordinary iTerm2 launches request the same wheel-only session settings. CLion/JediTerm and other terminals also enable real wheel reports by default,
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

Shortcut conventions follow the official [GNU Readline history](https://www.gnu.org/software/bash/manual/html_node/Commands-For-History.html),
[movement](https://www.gnu.org/software/bash/manual/html_node/Commands-For-Moving.html),
and [killing/yanking](https://www.gnu.org/software/bash/manual/html_node/Commands-For-Killing.html)
documentation. Wheel/selection behavior follows [iTerm2 Terminal preferences](https://iterm2.com/documentation-preferences-profiles-terminal.html)
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

Press Left Arrow on an empty prompt to open the command center. It starts with
**Models**: all project checkpoints, their sizes, update dates and compatibility. The selected model also shows saved
episode/update counts and its stored best score when available.
Use Up/Down to select, Enter to watch, E to evaluate, T to resume training, and R
to refresh. Incompatible checkpoints remain visible but cannot be run. These actions
reuse the same Release build and workers as the slash commands. Model checks show
an immediate loading acknowledgement and remain cancellable with Q or Esc; the
loading screen also updates when the terminal is resized.

Press 2 for **Model logs**, or 1 to return to Models. Logs contain model
training, evaluation, critic learning, and policy playback only. Builds, shell
commands, and file previews stay in the normal transcript and Ctrl+O output view.
Training entries show completed episode rewards, update statistics, deterministic
survival, saved checkpoints and best scores; per-step telemetry is omitted.
Critic entries show training/validation MSE, the constant baseline and actor
integrity checks. Logs are session-only and follow the transcript size limit.
Filters show All, Training, Evaluation, Critic and Errors with real counts.
Tab / Shift+Tab or Left/Right switches filters; Up/Down selects a model event.
Wide terminals show model results beside the list. Enter opens model-event details;
Up/Down and Page Up/Down scroll it. Q returns to the list; Esc returns directly
to your prompt. During name/value editing, Esc cancels the edit first and Q
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
Compatibility describes the interface, not model performance. Evaluation history
is compared only when the observation, action, and motor configurations match.

In the training Overview, `T` expands or collapses the latest joint targets.
Targets start collapsed to leave room for the survival and loss graphs.
The best-evaluation card keeps a compact height independently of target details.

### Paired critic-baseline gradient diagnostic (Step 111)

This is a developer experiment, separate from actor training and the Critic Lab:

```sh
PYTHONPATH=build-release:. python3 -m python.gradient_experiment --output docs/gradient-step111-new.json
```

Choose a new output name; existing reports and checkpoints are never replaced.
The experiment reconstructs Step 110's frozen seed-42 actor and seed-43 critic
(32 training episodes, 16 validation episodes, exploration seeds 4201/4202,
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
