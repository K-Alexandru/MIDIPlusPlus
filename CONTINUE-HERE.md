# Start here

Updated 2026-09-18 on `claude/consolidate-2026-09-15`, the one branch. Read
`HANDOFF.md` only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

## Goal

A tester reports one note sounding as two: find it, fix it under a test,
rebuild demo v2, then finish publishing the source. QuartzMIDI is the ImGui
shell (`ui/`) over `PlaybackCore`, replacing the Win32 window (`MIDI++/`).

## Seats

- Panel seat owns `ui/`; Claude owns `MIDI++/`, `tests/`, `tools/`, specs and
  engine seams in `ui/`. Every `ui/` change so far was owner-asked.

## Decisions made, do not reopen

- The name is QuartzMIDI everywhere a user sees it. MIDI++ survives only in
  source tree names (`MIDI++/`, `MIDIShell.vcxproj`, the window class), credits.
- Nothing sent out carries an account name, an assistant name or a working
  doc. Commits carry no `Co-Authored-By` line from here on, in any repository.
- The public source goes to `greasebob/QuartzMIDI`, the owner's alt account:
  a new repository, not a fork, one fresh commit, holding only what builds the
  app (the source zip's tree, a README, a `.gitignore`). Author and committer
  `greasebob <240342428+greasebob@users.noreply.github.com>`. This history
  stays out: 255 commits carry the owner's email and 228 an assistant line.
- The owner sees the commit metadata, the README and a name scan and says yes
  before the repository exists; a public page is shown before it is saved.
- UI copy is the owner's voice (`HANDOFF.md` section 15): no explanatory text
  anywhere, not even why a control is disabled; redesign the control.
- Key Mapping never opens on its own; Solo Piano is one toggle; skins are
  colour only; the window floor is 884 x 560 and mini has one size per mode.
- AutoVol is global and stays on across a load (owner, 2026-09-18, after a
  tester had to recalibrate per MIDI). A load never sweeps the volume keys.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- The note path: `MIDI++/PlaybackCore.cpp` (`play_notes`, `execute_note_event`,
  `release_keys`), `MIDI++/MIDI2Key.cpp` (`ProcessMidiMessage`, `SetActive`),
  `MIDI++/MidiInput.cpp`, `MIDI++/MIDIConnect.cpp`, `ui/ShellEngine.cpp`.
- `tests/ShellTests.cpp` captures injection in process; `FakeMidiInput` and
  `RecordedMidi` fake the ports. `tests/LatencyTests.cpp` uses loopMIDI.
- Run `tests\run-shell-tests.ps1 -Render`, then `tools\make-release.ps1` and
  `python tools\make-source.py`. `tools/colab-redirect.ipynb` is the notice.

## Verified facts

- Changed on the note path on 2026-09-18 and not yet examined for this bug:
  autoplay batches inject inline on the playback thread and wait on a
  high-resolution timer; `MIDI2Key` drains callbacks and releases held keys on
  re-arm and channel change; the shell now compiles RtMidi's WinMM backend
  (`__WINDOWS_MM__`), so a device can appear on WinRT and on WinMM.
- `gh` holds two accounts, K-Alexandru active. Git itself uses Credential
  Manager, which now offers greasebob and is refused by `origin`; push with
  `git -c credential.helper= -c credential.helper="!gh auth git-credential"`.
- A running `QuartzMIDI.exe` blocks the shell link and `make-release.ps1`.
  Ask the owner to close theirs; never kill it.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a
  click. PowerShell 5.1: never redirect a native exe with `2>&1` under
  `$ErrorActionPreference = 'Stop'`; run `run-shell-tests.ps1` bare.
- Windows SDK 10.0.26100 sits beside 10.0.22621 and the projects' `10.0` means
  26100. It links C++/WinRT where 22621 loads it, so `<winrt/>` files name
  `runtimeobject.lib`. `make-source.py` builds its zip once per SDK, through
  the zip's own `QuartzMIDI.sln`. The v145 toolset is untried.
- A release copies the tracked `x64\Release\config.json` and refuses if it
  is modified; `build\shell\config.json` is rewritten by every run.

## Work completed

- To 2026-09-17: see `HANDOFF.md`.
- 2026-09-18: a bug, UI and performance pass, recorded with what is left in
  `REVIEW-2026-09-18.md`, timing in `LATENCY.md`; a source zip a tester can
  build, their Visual Studio 2026 LNK2019 reproduced and fixed; the notice.

## Unresolved

- **The doubled note.** Nothing is known yet: autoplay or live, which file or
  device, keystrokes or MIDI output, which build.
- **Publishing, stopped before staging.** Unzip `QuartzMIDI-source-5b12020.zip`,
  add a short README (upstream's claims latency this fork never measured)
  and a `.gitignore`, commit as above, show the owner, then
  `gh repo create greasebob/QuartzMIDI --public`.
- **The Colab** (`colab.research.google.com/drive/1YNebID6yrtsqjCnXO5feFrG8LpLJqUR1`):
  put the new address in the notebook's three `PROJECT_URL`s, get the owner's
  yes on its wording, edit it in place in their Chrome so the link holds.
  The extension is connected; whether it is signed in as the owner is unchecked.
- **Tester on Visual Studio 2026** to build `5b12020`, or send the error text.
- **Owner to read** the text redrawn (UI, in the review) and test `8baf9bd`:
  velocity editor, WinMM, mini, sheets, game, Wooting, MIDI out, mixed DPI.

## Validation actually run

- `8baf9bd`: `ShellTests.exe`, parity, 360 render scenarios, the native suite
  and `LatencyTests.exe --loopback` PASS; eleven new tests fail with their fix
  reverted; the release exe carries no account, assistant or path string.
- `5b12020`: the source zip builds on both SDKs; the zip before the fix shows
  the tester's LNK2019 on 26100; every suite PASSes built on 26100.
- `d813c05`, after demo v2: AutoVol across a load, `ShellTests.exe` PASS.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus, public; `upstream` is
  Zephkek/MIDIPlusPlus. `main` stays at `e37ba7e`. Pushing to `origin` without
  asking is authorized; creating the public repository is not.
- The main checkout is on this branch, pushed, clean apart from the owner's
  `x64\Release\MIDI++.exe` and `x64\Release\midi\`; leave `D:\Dev\mpp-panels`.
- Demo v2 from `8baf9bd`: `build\release\QuartzMIDI-demo-v2.zip` (426 MB,
  SHA256 `14237B7FCDD2D6C98604CA83D472BCCC885B9E9A5186FFFFB5AA6F3E4DC4164C`)
  and `QuartzMIDI-source-5b12020.zip`; send no older source zip.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Ask the owner for the tester's report of the doubled note (autoplay or live,
the file or device, keystrokes or MIDI output, which build), then reproduce it
as a failing test in `tests/ShellTests.cpp` before changing any code.
