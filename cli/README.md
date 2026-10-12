# Aura CLI

A C++20 terminal workspace for local tools. The full-screen interface has a
compact welcome header, a persistent directory/version header, a fixed bottom composer,
and command/file menus. Beneath the composer, a status line shows Ready/Shell/Running,
elapsed time while running, queued inputs, completed/failed/stopped runs, and the Git branch.
The workspace path stays in the header. Git
metadata is read locally, including worktrees and detached HEAD, without running
a shell command. The context line stays outside the gray composer background.
Normal and shell input share the same gray area with equal padding above and
below the text. Main UI text uses the terminal defaults, with soft warm, green,
and lavender colors in the context line; bold/dim text establishes hierarchy. UTF-8 terminals show one
compact rounded welcome card; ASCII mode uses a borderless header.
Failures use red text in error messages, completion summaries, and command center
results. User-requested stops remain neutral.
Set `NO_COLOR=1` to use only the terminal's default text colors. No model or AI service is connected.

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
muted body. Empty output explicitly shows `(no output)`. Long shell output shows six wrapped lines followed by `+ Show details` and the
number of hidden lines. Click Show details / Show less to toggle that entry.
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

### Voice dictation (macOS)

Run `/voice`, allow the macOS microphone and speech-recognition prompts, then
speak. The waveform follows microphone volume and partial transcription appears
inside the composer. **Enter** stops recording, briefly finishes recognition,
and leaves the text in your editable draft. **Esc** or **Ctrl+C** discards this
dictation. Nothing sends automatically; review the draft, then use Enter or Tab.
Output scrolling and clickable Details remain available while listening.

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
| `/control` | Open a table to edit the four live joint targets. |
| `/train_balance` | Search balance feedback gains without a 3D window. |
| `/status` | Show directory, runtime, shell, and model status. |
| `/files` | List files in the current directory. |
| `/cd <directory>` | Change workspace; quoted paths and `~` are supported. |
| `/history` | Show inputs from this session. |
| `/queue` | Show inputs waiting to run. |
| `/unqueue` | Restore the last queued input to an empty draft. |
| `/version` | Show the CLI version. |
| `/clear` | Clear the transcript and show the welcome area. |
| `/exit` | Close Aura CLI and restore the terminal. |

`/pwd` aliases `/status`; `/quit` aliases `/exit`. Commands also work as arguments,
for example `aura-cli --help` or `aura-cli status`.

### Balance feedback search

`/train_balance` builds the Python extension if needed, then compares a neutral
baseline with 20 seeded Kp/Kd pairs. Each controller computes a symmetric hip
correction every physics step: `clamp(kp * up_z - kd * omega_x, -0.2, 0.2)`.
Knees stay neutral. Episodes score survival up to 1200 steps (10 simulated seconds).

After the build, a full-screen Balance Lab shows current and best gains, live
hip correction, height, uprightness, episode step count, and trial outcomes.
`LIMIT` marks a correction that was clamped. Scores are compared with the neutral
baseline measured in this run and the historical 346-step fixed-target record.
The best trial has a permanent `*` marker and accent color. Up/Down or K/J
selects trials with a highlighted row; the wheel moves selection by three rows.
Page Up/Down selects a page away; Home/End selects the first/last result.
The best trial is selected automatically when the search finishes unless you
have already navigated.

Enter replays the selected completed trial in 3D. Replay uses the original full-precision gains and controller function
with a reset simulation. An unfinished search pauses during replay and resumes
afterward. Replay returns automatically to the table when the episode ends, with
the score and outcome shown beneath the table. Esc works in the 3D window as well as the
terminal; S or closing the replay window also returns early. Enter can replay
the same trial again. Completed search windows close rather than staying frozen
on the final pose. The worker retains one hidden viewer and polls macOS events
between replays; this keeps the GUI process responsive during the handoff to the
terminal. The idle dashboard sends no repeated cursor redraw commands.

S or Ctrl+C stops the search and keeps completed trials available for replay.
Q or Esc leaves the training screen. Results remain available while the screen
is open and are not saved to disk automatically.
Small terminals use a compact screen; redirected output keeps a plain report.
A summary with the best gains remains in the transcript when you leave.
Best gains are not saved to a file automatically.

Run `python3 tests/cli_balance_smoke.py` to exercise real CLI selection, mouse
input, replay, cancellation, resizing, and repeated Stop requests.
Add `--complete` to include a full 21-episode search and replay afterward.
The test requires a graphical session for the replay windows.

Controller experiments live in `python/train_balance.py`; viewer pacing and
CLI progress reporting live in `python/balance_session.py`. Calling
`evaluate_feedback(simulation, kp, kd)` directly still works without the UI.

The search runs without a 3D window. Only selecting a trial and pressing Enter
opens the viewer, which replays the same controller at watchable speed.

### Joint controls

`/control` first runs `python3 -m python.control_aura --prepare`, showing the
controller's build and launch output. The Python controller configures and builds
AURA, sets neutral targets, and opens the viewer. Only after successful startup
does the CLI open the dedicated control screen. Failed or interrupted startup
returns to the prompt. Startup output remains available in the CLI transcript.

On terminals at least 121 columns by 28 rows, the top right shows a rotating
dotted schematic using the live viewer’s part transforms. The rotation is a
preview camera orbit. Observation feedback shows pelvis height,
uprightness, standing status, data freshness, simulation time, steps, speed,
spin, and pause state. The LIVE line reports connection status. Smaller terminals keep the
compact joint editor.

The control screen temporarily replaces the chat viewport and restores your
transcript when you leave. It shows live pelvis height, uprightness, and standing
status. Each joint row shows the measured angle beside the commanded target and
its allowed range. Viewer status changes from waiting to live, then stale if its
observation file stops updating.

- ↑/↓ selects a joint.
- 1–4 selects a joint directly.
- ←/→ changes its target by the selected step and applies immediately. Press S
  to cycle through 0.01, 0.05, and 0.20 radians.
- ↵ opens the selected value for editing; type an angle and press ↵ to apply.
  Esc cancels an unfinished edit.
- R sets all four targets to neutral.
- The Error column shows target minus actual angle. A LIMIT marker remains
  beside a target that was clamped.
- Esc, Q, or Ctrl+C returns to the CLI. Applied targets stay active.


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
- Option-drag selects terminal text; Cmd+C copies the selection. Selection uses a
  light neutral gray highlight with dark text in iTerm2, including over gray input boxes.

Long drafts wrap upward into at most six lines. The prompt stays at the bottom
while output is browsed. Drag normally to select text and use the terminal's
Copy shortcut.

### iTerm2: clickable Details and separate wheel scrolling

Run `./cli/install.sh --iterm2` to install/update the dedicated **Aura CLI** dynamic
profile, then open a new **Profiles > Aura CLI** session in iTerm2. It launches the
installed binary with `AURA_MOUSE=click` and inherits the default profile's
appearance. The installer requires Python 3 only for generating this macOS profile.

The profile enables mouse wheel and click reporting. Click a Show details / Show
less row to toggle that specific entry. Expanded file previews begin at the top
of the file. Mouse gestures scroll only the output viewport, physical Up/Down
edit history, and the prompt stays fixed. Hold **Option** while dragging to select
text, then use the terminal's Copy shortcut. This tradeoff was selected for the
clickable interface. The main profile and global iTerm2 settings are not modified.
Mode 1007 (wheel-to-arrow translation) is disabled so scrolling cannot recall inputs.

Ordinary `aura-cli` launches also enable wheel scrolling and clickable Details.
In iTerm2, Aura requests session mouse reporting and disables wheel-to-arrow
translation at launch. When `ITERM_PROFILE` is available, it restores that saved
profile on exit. Saved profiles and global preferences are not changed.
The dedicated profile ensures iTerm2 permits these reports. Hold Option to select
text. Set `AURA_MOUSE=off` to disable mouse reporting and use Ctrl+O for Details.
The earlier wheel-only
mode remains supported with `AURA_MOUSE=wheel` in an iTerm2 profile where wheel
reporting is enabled but click/drag reporting is disabled.

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
- `/queue` and `/unqueue`: inspect waiting inputs or restore the last to the draft.

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
persistent title and workspace line. `/clear` restores the welcome screen.
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

Press Left Arrow on an empty prompt to browse this session's file previews, shell runs, and
queued inputs. Filters show All, Finished, Failed, Stopped, and Queued with real
counts. Tab / Shift+Tab or Left/Right switches filters; Up/Down selects a row.
Wide terminals show run details beside the list. Enter opens captured output;
Up/Down and Page Up/Down scroll it. Esc returns to the list, then to your prompt.
With a nonempty draft, Left Arrow moves the cursor normally. Native text
selection remains available with Option in the clickable profile.

Each shell run records its command, working directory, exit code, elapsed time,
and finish time. Opening a run does not execute it again. The center is local to
the current process; `/clear` clears its completed runs and their output. Queued
inputs remain visible until executed or cancelled. Open the center after the
active command finishes.
