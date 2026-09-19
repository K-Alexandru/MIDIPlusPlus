# Start here

Updated 2026-09-18 on `claude/consolidate-2026-09-15`, the one branch. Read
`HANDOFF.md` only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

## Goal

Build the seven items `SHELL-GAPS.md` lists under "Asked for on 2026-09-18", in
its order. Hotkeys, custom themes and Legit mode are built, none fully pressed;
the owner is testing Legit mode now. Items 4 to 7 are planned, not started. The
panel seat owns `ui/` (`SEATS.md`); what the owner asks for there is in scope.

## Decisions made, do not reopen

- The name is QuartzMIDI everywhere a user sees it; MIDI++ survives only in
  source tree names and credits.
- Nothing sent out carries an account name, an assistant name or a working
  doc. Commits carry no `Co-Authored-By` line, in any repository.
- The public source goes to `greasebob/QuartzMIDI`: a new repository, one fresh
  commit, author and committer
  `greasebob <240342428+greasebob@users.noreply.github.com>`, the owner's yes on
  metadata, README and a name scan first. Parked: features come before it.
- UI copy is the owner's voice (`HANDOFF.md` section 15): no explanatory text,
  not even why a control is disabled; redesign the control. Every name is plain
  and professional: "Auto-generate Dark", "Hold Tapped Notes", "Troubleshooting".
- Hotkeys (item 1): no media key is a default; the legend is key plus icon in
  one outline at every width. Custom themes (item 2) use the app's own picker;
  item 6 reopens "colour only": a theme may change the whole design.
- Legit mode (`LEGIT-MODE.md`) is a fresh live take of a human recording, never
  a repair of a poor file. It leaves the app as a signed add-on in an `addons`
  folder, handed out by the owner; the word is never "mods". Speed and Hands
  stay; the variation, the players, Difficulty, Hold and Tap leave (item 3).
- The split does not start until the owner has tested Legit mode and says so,
  and nothing new of Legit is pushed until then.
- Help (item 7) is one button of questions in professionally named folders, a
  build's additions a filter chip and never a folder called New, plus a
  first-open tour with Skip, replayable from Help. No general mod system.
- The game owns the key protocol: Alt is velocity, Ctrl is the 88-key notes,
  the velocity keys are the note keys. A fix or a test never changes a bind.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- Legit mode: `LEGIT-MODE.md`, `MIDI++/LegitTake.hpp` (the take, a pure
  function), `prepare_event_queue`, `play_notes` and `tap_step` in
  `MIDI++/PlaybackCore.cpp`, `applyLegit` and the poller in `ui/ShellEngine.cpp`,
  `Segments` and `EstimatedSlider` in `ui/Panels.cpp`.
- Themes: `ui/ThemeModel.hpp`, `MIDI++/Skin.hpp`. Hotkeys: `ui/HotkeyNames.hpp`.
- Run `tests\run-shell-tests.ps1 -Render` (PNGs land in `build\render-tests`),
  `tests\run-latency-tests.ps1 -Legit`, then `tools\make-release.ps1` and
  `python tools\make-source.py`. `tools\measure-midi.py` measures a MIDI folder.

## Verified facts

- Plain playback keeps a recording's timing to under 0.4 ms at p99 (`run-latency-
  tests.ps1 -Fidelity`). Speed is a clock rate; `note_events` is never rescaled.
- `origin` is public and holds every Legit commit from `c60e46d` on. The app is
  GPLv3 by descent, so whoever is given a closed add-on may be owed its source.
- `ShellTests.exe` stops at its first failure; run the exe to read its output.
  Score commands such as Speed are dropped unless they carry the generation.
- `gh` holds two accounts, K-Alexandru active. Credential Manager offers
  greasebob and `origin` refuses it; push with
  `git -c credential.helper= -c credential.helper="!gh auth git-credential"`.
- A running `QuartzMIDI.exe` blocks the shell link and `make-release.ps1`; ask
  the owner to close theirs, never kill it. The display runs at 125%; read
  `tests/NativeShell.ps1` before scripting a click. No `2>&1` on a native exe.

## Work completed

- To `9d0f64f`: see `HANDOFF.md` and `git log`; hotkeys and custom themes last.
- 2026-09-18, `c60e46d` to `3bc3cf1`: Legit mode rebuilt as a take made ahead
  of time, Speed as a slider, Hands, Hold and Tap, settings kept per song.
- `d288d2b` to `a453281`: plans for the split and items 6 and 7; two labels renamed.

## Unresolved

- **Legit mode: owner to listen** to Pro, Student and Beginner on his eight
  recordings in `x64\Release\midi\`, and to press Hands, Hold and Tap in the
  game; the tap keys have only met a key table. The native suite was not run.
- **Owner to answer before the add-on goes to anyone:** the GPL question, what
  the Help entry tells people to do ("DM me" needs a name), and a yes to
  force-pushing this branch to `origin` without the Legit code after the split.
- **Hotkeys: only F5 and F6 were bound in the app.** Unpressed: a media key, a
  held key, move, unbind, Escape, repeat, capture with Settings in mini mode.
- **Themes: the picker has never been dragged**, nor a theme saved or deleted.
- **Owner to say** whether a note key may be a hotkey, and if light mode is
  dark enough; `skin-system.html` then follows.
- **Tester shampoojr** to confirm the tester list; none of it has met a real
  Wooting or Roblox. Testers have been quiet, so ask them directly.
- **Publishing**, parked, and the Colab notice behind it. `build\publish` holds a
  staged commit whose README and `.gitignore` are to keep; restage it first.
- **Tester on VS 2026** to build the source zip. **Owner to test**: velocity
  editor, WinMM, mini, sheets, MIDI out, mixed DPI.

## Validation actually run

- `3bc3cf1`: `ShellTests.exe`, 500 render scenarios, `LatencyTests.exe --legit`
  PASS; the `legit-disabled-on-load` mutant killed by hand; new renders read at
  125%; the source zip builds on both SDKs. After it, only a `RenderTests`
  compile at `a453281`. The native suite last passed at `8baf9bd`.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus, public; `upstream` is
  Zephkek/MIDIPlusPlus. `main` stays at `e37ba7e`. Pushing to `origin` without
  asking is authorized; creating a repository and force-pushing are not.
- This branch is pushed and clean apart from the owner's
  `x64\Release\MIDI++.exe` and `x64\Release\midi\`; leave `D:\Dev\mpp-panels`.
- Demo v2 from `3bc3cf1`: `build\release\QuartzMIDI-demo-v2.zip`, SHA256
  `0FD2997D91395F7EA363AA850EC47EC49C1D5A967D16DADC6CB4DC6A7B7DE7F4`, and
  `QuartzMIDI-source-3bc3cf1.zip`; older pairs are stale. Two labels changed since.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Ask the owner what he heard and felt testing Legit mode, Hands, Hold and Tap on
the `3bc3cf1` build, and fix what he reports in `MIDI++/LegitTake.hpp` or the
controls before anything else.
