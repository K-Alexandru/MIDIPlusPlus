# Start here

Updated 2026-09-18 on `claude/consolidate-2026-09-15`, the one branch. Read
`HANDOFF.md` only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

## Goal

Build the five items `SHELL-GAPS.md` lists under "Asked for on 2026-09-18", in
its order. Rebindable hotkeys are built (`ed08fca`); custom colour themes are next.
QuartzMIDI is the ImGui shell (`ui/`) over `PlaybackCore`. The panel seat owns
`ui/` (`SEATS.md`); the owner asked for these controls, so their `ui/` work is in scope.

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
  not even why a control is disabled; redesign the control.
- Hotkeys, built: `SHELL-GAPS.md` item 1. No media key is ever a default, and
  the legend is icons only and never shows the seek seconds.
- Custom themes: every colour editable in a friendly way, text included, seen
  live. A theme has a light and dark pair or not; without one the light/dark
  toggle is greyed out while it is selected; with one the other half is edited
  by hand or chosen automatically as the opposite of the half being edited.
  A custom theme is still colour only, like every skin.
- The game owns the key protocol: Alt is velocity, Ctrl is the 88-key notes,
  the velocity keys are the note keys. A fix or a test never changes a bind.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- Themes: `MIDI++/Skin.hpp`, `ui/SkinDraw.cpp`.
- `tests/ShellTests.cpp` captures injection in process; `tests/RenderTests.cpp`
  writes a PNG per scenario to `build\render-tests`.
- Run `tests\run-shell-tests.ps1 -Render`, `tools\make-release.ps1`, `python tools\make-source.py`.

## Verified facts

- The engine worker owns `config.json`.
- Skins: preferences store an index into `skin::All()`, `skin ^= 1` is the
  light/dark toggle, and Settings picks a colour by `skin < 2`.
- `ShellTests.exe` stops at its first failure and buffers output: run the exe
  itself to read what it printed.
- `gh` holds two accounts, K-Alexandru active. Credential Manager offers
  greasebob and `origin` refuses it; push with
  `git -c credential.helper= -c credential.helper="!gh auth git-credential"`.
- A running `QuartzMIDI.exe` blocks the shell link and `make-release.ps1`.
  Ask the owner to close theirs; never kill it.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a
  click. PowerShell 5.1: never redirect a native exe with `2>&1` under
  `$ErrorActionPreference = 'Stop'`; run `run-shell-tests.ps1` bare.

## Work completed

- To 2026-09-17: `HANDOFF.md`. 2026-09-18: `REVIEW-2026-09-18.md`, `LATENCY.md`.
- 2026-09-18, `b674608` to `4fafdb7`, from one tester: Wooting strike speed over
  10 to 20ms; the Roblox tab-out gate; a velocity tap never lands on a held
  note key; a stable load sort; notes of no length released in playback; one
  strike for two tracks on a key; Roblox preselected in AutoVol; darker light skins.
- 2026-09-18, `3fc9e86` to `ed08fca`: six rebindable hotkeys with media keys;
  the legend is a key and its action's icon in one outline at every width,
  never words, caps alone or seek seconds; `make-source.py` drops `MIDI++.APS`.

## Unresolved

- **Hotkeys: the owner bound F5 and F6 in the app; the rest is unpressed.** The
  Sol seat or the owner: a media key, a key another program holds, move a key
  between actions, unbind, Escape out, hold the key past its repeat, and
  capture with Settings as its own window in mini mode.
- **Owner to say** whether a note key may be a hotkey: bound, it is taken from
  the game while the app is open, and nothing stops it today.
- **Tester shampoojr** to confirm that list on the `ed08fca` build; none of it
  has met a real Wooting or Roblox. His doubled note was never reproduced.
- **Owner to say** whether light mode is dark enough; then `skin-system.html`,
  which shows the old values, takes the new ones.
- **Publishing**, parked, and the Colab notice behind it. `build\publish` holds a
  staged commit whose README and `.gitignore` are to keep; restage it first.
- **Tester on Visual Studio 2026** to build the source zip or send the error.
- **Owner to test**: velocity editor, WinMM, mini, sheets, MIDI out, mixed DPI.

## Validation actually run

- `8baf9bd`: `ShellTests.exe`, parity, 360 render scenarios, the native suite
  and `LatencyTests.exe --loopback` PASS.
- `b674608` to `4fafdb7`: `run-shell-tests.ps1 -Render` PASS at each commit. The
  Wooting, tap, load sort, playback and one-strike tests fail with their fix
  reverted; the AutoVol preselect has no test. The tester's own MIDI leaves no
  key owned through the real loader.
- `319740e` to `ed08fca`: `run-shell-tests.ps1 -Render` PASS at each, 420 render
  scenarios; `settings-hotkeys`, `minimum-six-keys` and `mini-six-keys` read at
  125% in one skin each. The restart test fails with the `validate` change
  reverted. Capture is tested against a key table, never `GetAsyncKeyState`.
  The source zip builds on both SDKs.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus, public; `upstream` is
  Zephkek/MIDIPlusPlus. `main` stays at `e37ba7e`. Pushing to `origin` without
  asking is authorized; creating the public repository is not.
- This branch is pushed and clean apart from the owner's
  `x64\Release\MIDI++.exe` and `x64\Release\midi\`; leave `D:\Dev\mpp-panels`.
- Demo v2 from `ed08fca`: `build\release\QuartzMIDI-demo-v2.zip`, SHA256
  `2DCB46AE4202FFC5DFC58603AE69752AA2DD792993F3948127D4DDB06BED6DAD`, and
  `QuartzMIDI-source-ed08fca.zip`; send no older pair.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Custom themes, from the decision above. Read `MIDI++/Skin.hpp` and every use of
`preferences.skin` and `skin::All()`, then write in `SHELL-GAPS.md` item 2 how a
skin is named once the fixed index gives way, before touching code.
