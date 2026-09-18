# Start here

Updated 2026-09-18 on `claude/consolidate-2026-09-15`, the one branch. Read
`HANDOFF.md` only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

## Goal

Build what `SHELL-GAPS.md` lists under "Asked for on 2026-09-18", hotkeys
first; publishing the source waits for the owner. QuartzMIDI is the ImGui
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

- `play_notes` runs a batch's releases before its presses; a release whose own
  press is in the batch runs after them (`a187c94`). That order, not the load
  sort, is what stuck a tester's key. `ShellTests.exe` stops at its first
  failure and buffers output: run the exe itself to read what it printed.
- The game owns the key protocol: Alt is velocity, Ctrl is the 88-key notes,
  and the velocity keys are the note keys. A fix never changes a bind.
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

- See `HANDOFF.md`, then `REVIEW-2026-09-18.md` and `LATENCY.md` for 2026-09-18.

## Unresolved

- **Tester shampoojr** to confirm on a build from `4fafdb7` or later: Wooting
  velocity, tab-out, the 0 key on Everything Will Freeze, AutoVol's list.
- **Publishing waits for the owner.** `build\publish` holds a staged commit
  with the README and `.gitignore` to keep; restage it from the newest source
  zip, show the owner, then `gh repo create greasebob/QuartzMIDI --public`.
- **The Colab** (`colab.research.google.com/drive/1YNebID6yrtsqjCnXO5feFrG8LpLJqUR1`):
  put the new address in the notebook's three `PROJECT_URL`s, get the owner's
  yes on its wording, edit it in place in their Chrome so the link holds.
  The extension is connected; whether it is signed in as the owner is unchecked.
- **Tester on Visual Studio 2026** to build the next source zip, or send the error text.
- **Owner to read** the text redrawn (UI, in the review) and test `8baf9bd`:
  velocity editor, WinMM, mini, sheets, game, Wooting, MIDI out, mixed DPI.

## Validation actually run

- `8baf9bd`: `ShellTests.exe`, parity, 360 render scenarios, the native suite
  and `LatencyTests.exe --loopback` PASS; eleven new tests fail with their fix
  reverted; the release exe carries no account, assistant or path string.
- `5b12020`: the source zip builds on both SDKs; the zip before the fix shows
  the tester's LNK2019 on 26100; every suite PASSes built on 26100.
- `d813c05`, after demo v2: AutoVol across a load, `ShellTests.exe` PASS.
- `b674608` to `4fafdb7`, all from one tester, `-Render` PASS at each: Wooting
  strike speed over 10 to 20ms; key downs only with Roblox in front; a tap
  skips held keys; notes of no length; one strike for two hands on a key;
  Roblox first in AutoVol; darker light skins. None has met a Wooting or Roblox.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus, public; `upstream` is
  Zephkek/MIDIPlusPlus. `main` stays at `e37ba7e`. Pushing to `origin` without
  asking is authorized; creating the public repository is not.
- The main checkout is on this branch, pushed, clean apart from the owner's
  `x64\Release\MIDI++.exe` and `x64\Release\midi\`; leave `D:\Dev\mpp-panels`.
- Demo v2 from `b0b5d22`: `build\release\QuartzMIDI-demo-v2.zip` (426 MB,
  SHA256 `B7D2238071C760BEC28FB0E9AFEAB91D6433BA5A15B14446ED23D8A3E18DCB72`)
  and `QuartzMIDI-source-b0b5d22.zip`; both are two commits behind, see Next.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Rebuild both zips from `4fafdb7` or later once the owner's app is closed (the
`b0b5d22` pair lacks `a187c94`, the real sticky-key fix, and the darker light
skins). Then item 1 of the 2026-09-18 list in `SHELL-GAPS.md`: hotkeys.
