# Start here

Updated 2026-09-18 on `claude/consolidate-2026-09-15`, the one branch. Read
`HANDOFF.md` only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

## Goal

Build the six items `SHELL-GAPS.md` lists under "Asked for on 2026-09-18", in
its order. Hotkeys, custom themes and Legit mode are built, none fully pressed;
Legit mode has never been heard. AutoVol's keys (item 4) are next. The panel
seat owns `ui/` (`SEATS.md`); what the owner asks for there is in scope.

## Decisions made, do not reopen

- The name is QuartzMIDI everywhere a user sees it; MIDI++ survives only in
  source tree names and credits.
- Nothing sent out carries an account name, an assistant name or a working
  doc. Commits carry no `Co-Authored-By` line, in any repository.
- The public source goes to `greasebob/QuartzMIDI`: a new repository, one fresh
  commit of what builds the app, author and committer
  `greasebob <240342428+greasebob@users.noreply.github.com>`, the owner's yes
  on metadata, README and a name scan first. Parked by the owner on
  2026-09-18: fixes and features come before the push.
- UI copy is the owner's voice (`HANDOFF.md` section 15): no explanatory text,
  not even why a control is disabled; redesign the control. Names are plain
  and professional: "Auto-generate Dark", not "Dark follows this one".
- Hotkeys (`SHELL-GAPS.md` item 1): no media key is a default; the legend is
  key plus icon in one outline at every width, never words or seek seconds.
- Custom themes (item 2): colour only, like every skin; the swatches and the
  picker are the app's own drawing, not ImGui's widgets.
- Legit Mode (`LEGIT-MODE.md`) is a fresh live take of a human recording,
  never a repair of a poor file. It is to leave the app and become a signed,
  DM-only mod; Speed and Hands stay (`SHELL-GAPS.md` item 3 has the plan). Not
  to start until the owner has tested it and says so. Push nothing new of it.
- The game owns the key protocol: Alt is velocity, Ctrl is the 88-key notes,
  the velocity keys are the note keys. A fix or a test never changes a bind.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- Legit mode: `LEGIT-MODE.md`, `MIDI++/LegitTake.hpp` (the take, a pure
  function), `prepare_event_queue`, `play_notes` and `tap_step` in
  `MIDI++/PlaybackCore.cpp`, `applyLegit` and the poller in `ui/ShellEngine.cpp`.
- Themes: `ui/ThemeModel.hpp`, `ThemeSwatch` and `DrawThemeEditor` in
  `ui/Panels.cpp`. Hotkeys: `ui/HotkeyNames.hpp`, `ui/Shell.cpp`.
- Run `tests\run-shell-tests.ps1 -Render` (PNGs land in `build\render-tests`),
  then `tools\make-release.ps1` and `python tools\make-source.py`.

## Verified facts

- Plain playback keeps a recording's timing to under 0.4 ms at p99 (`run-latency-
  tests.ps1 -Fidelity`). Speed is a clock rate; `note_events` is never rescaled.
- `ShellTests.exe` stops at its first failure; run the exe to read its output.
- `gh` holds two accounts, K-Alexandru active. Credential Manager offers
  greasebob and `origin` refuses it; push with
  `git -c credential.helper= -c credential.helper="!gh auth git-credential"`.
- A running `QuartzMIDI.exe` blocks the shell link and `make-release.ps1`.
  Ask the owner to close theirs; never kill it.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a
  click. PowerShell 5.1: no `2>&1` on a native exe; run the test script bare.

## Work completed

- To 2026-09-17: `HANDOFF.md`, `REVIEW-2026-09-18.md`, `LATENCY.md`; then
  `b674608` to `4fafdb7`, eight tester fixes; Legit redesign agreed 2026-09-18.
- 2026-09-18, `3fc9e86` to `9d0f64f`: rebindable hotkeys with an icon legend,
  custom themes with the app's own picker, wider hover fills in Settings, the
  Legit Mode tag, and `make-source.py` dropping `MIDI++.APS`.
- 2026-09-18, `c60e46d` to `3bc3cf1`: Legit mode rebuilt as a take made ahead
  of time, Speed as a slider, Hands, Hold and Tap, settings kept per song.

## Unresolved

- **Legit mode: owner to listen** to Pro, Student and Beginner on his eight
  recordings in `x64\Release\midi\`, and to press Hands, Hold and Tap in the
  game; the tap keys have only met a key table. The native suite was not run.
- **Hotkeys: the owner bound F5 and F6; the rest is unpressed.** A media key, a
  key another program holds, move a key, unbind, Escape out, hold past repeat,
  and capture with Settings as its own window in mini mode.
- **Themes: the picker has never been dragged.** Field, bars, alpha under Fine
  detail, typed colour, both halves, unpaired, delete, `themes.json` on restart.
- **Owner to say** whether a note key may be a hotkey: bound, it is taken from
  the game while the app is open, and nothing stops it today.
- **Tester shampoojr** to confirm the tester list on the `9d0f64f` build; none
  of it has met a real Wooting or Roblox. His doubled note was never reproduced.
- **Owner to say** if light mode is dark enough; `skin-system.html` then follows.
- **Publishing**, parked, and the Colab notice behind it. `build\publish` holds a
  staged commit whose README and `.gitignore` are to keep; restage it first.
- **Tester on VS 2026** to build the source zip. **Owner to test**: velocity
  editor, WinMM, mini, sheets, MIDI out, mixed DPI.

## Validation actually run

- `8baf9bd`: `ShellTests.exe`, parity, 360 render scenarios, the native suite
  and `LatencyTests.exe --loopback` PASS.
- `b674608` to `9d0f64f`: `run-shell-tests.ps1 -Render` PASS at each commit, 460
  render scenarios at the last; new renders read at 125% in one or two skins.
  Hotkey capture is tested against a key table. The source zip builds on both SDKs.
- `3bc3cf1`: `ShellTests.exe`, 500 render scenarios, `LatencyTests.exe --legit`
  PASS; the `legit-disabled-on-load` mutant killed by hand; new renders read.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus, public; `upstream` is
  Zephkek/MIDIPlusPlus. `main` stays at `e37ba7e`. Pushing to `origin` without
  asking is authorized; creating the public repository is not.
- This branch is pushed and clean apart from the owner's
  `x64\Release\MIDI++.exe` and `x64\Release\midi\`; leave `D:\Dev\mpp-panels`.
- Demo v2 from `3bc3cf1`: `build\release\QuartzMIDI-demo-v2.zip`, SHA256
  `0FD2997D91395F7EA363AA850EC47EC49C1D5A967D16DADC6CB4DC6A7B7DE7F4`, and
  `QuartzMIDI-source-3bc3cf1.zip`; send no older pair.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Take what the owner says after hearing Legit mode and pressing Hands, Hold and
Tap, and fix that first; on his word, the mod split in item 3. Otherwise item 4
of `SHELL-GAPS.md`: AutoVol's keys, where `MIDI2Key.cpp` hardcodes the arrows.
