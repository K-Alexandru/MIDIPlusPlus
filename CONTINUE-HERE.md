# Start here

Updated 2026-09-18 on `claude/consolidate-2026-09-15`, the one branch. Read
`HANDOFF.md` only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

## Goal

Build the five items `SHELL-GAPS.md` lists under "Asked for on 2026-09-18", in
its order. Hotkeys and custom themes are built (`9d0f64f`), not fully pressed.
Legit mode is next: its redesign is agreed with the owner, not built. The panel
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
- Legit Mode (`SHELL-GAPS.md` item 3) is a fresh live take of a human
  recording, never a repair of a poor file. Presets are the assumed player:
  Pro, Student, Beginner. Difficulty is an estimate the user overrides. The
  Experimental tag stays until the owner says it convinces.
- The game owns the key protocol: Alt is velocity, Ctrl is the 88-key notes,
  the velocity keys are the note keys. A fix or a test never changes a bind.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- Legit mode: `LEGIT-MODE.md`, `HANDOFF.md` line 352, the batch loop near line
  760 and the `legit_*` functions near 1390 of `MIDI++/PlaybackCore.cpp`,
  `LegitModeSettings` in `MIDI++/config.hpp`; tests `LatencyTests.exe --legit`.
- Themes: `ui/ThemeModel.hpp`, `ThemeSwatch` and `DrawThemeEditor` in
  `ui/Panels.cpp`. Hotkeys: `ui/HotkeyNames.hpp`, `ui/Shell.cpp`.
- Run `tests\run-shell-tests.ps1 -Render` (PNGs land in `build\render-tests`),
  then `tools\make-release.ps1` and `python tools\make-source.py`.

## Verified facts

- Legit mode today is three memoryless uniform draws at dispatch (press offset,
  2% dropped note-on, a 50 to 200 ms hesitation), done as `sleep_for` on the
  dispatch thread, so everything due waits behind them; a tester called it laggy.
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

## Unresolved

- **Legit mode: owner to say** whether the preset and Difficulty are remembered
  per file (nothing is saved per song today). Eight of his recordings are in
  `x64\Release\midi\`, measured by `tools\measure-midi.py` (item 3 has the numbers).
- **Hotkeys: the owner bound F5 and F6; the rest is unpressed.** A media key, a
  key another program holds, move a key, unbind, Escape out, hold past repeat,
  and capture with Settings as its own window in mini mode.
- **Themes: the picker has never been dragged.** Field, bars, alpha under Fine
  detail, typed colour, both halves, unpaired, delete, `themes.json` on restart.
- **Owner to say** whether a note key may be a hotkey: bound, it is taken from
  the game while the app is open, and nothing stops it today.
- **Tester shampoojr** to confirm the tester list on the `9d0f64f` build; none
  of it has met a real Wooting or Roblox. His doubled note was never reproduced.
- **Owner to say** whether light mode is dark enough; then `skin-system.html`,
  which shows the old values and knows nothing of themes, takes the new ones.
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
- Legit mode: nothing run; `LatencyTests.exe --legit` last ran before 2026-09-18.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus, public; `upstream` is
  Zephkek/MIDIPlusPlus. `main` stays at `e37ba7e`. Pushing to `origin` without
  asking is authorized; creating the public repository is not.
- This branch is pushed and clean apart from the owner's
  `x64\Release\MIDI++.exe` and `x64\Release\midi\`; leave `D:\Dev\mpp-panels`.
- Demo v2 from `9d0f64f`: `build\release\QuartzMIDI-demo-v2.zip`, SHA256
  `4003221F4C72023E16169A72E58C7F2B877CE0F61EFBCFEE022F6E0D0775BA4D`, and
  `QuartzMIDI-source-9d0f64f.zip`; send no older pair.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Read `SHELL-GAPS.md` item 3 and `LEGIT-MODE.md`. First measure whether plain
playback flattens a human recording (`LatencyTests.exe`, the owner's files),
then build the plan schedule beside `note_buffer` in `MIDI++/PlaybackCore.cpp`
with its tests, then the Settings controls, then rewrite `LEGIT-MODE.md`.
