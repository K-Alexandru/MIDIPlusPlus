# Start here

Updated 2026-09-15 on `claude/consolidate-2026-09-15`, the one branch every
earlier branch was folded into. Then read `HANDOFF.md` only where this file
points, `SHELL-GAPS.md` (what the shell owes) and `SEATS.md` (who does what).

## Goal

One Windows app that does everything the original MIDI++ window did and more.
The ImGui shell (`ui/`, `build\shell\MIDIShell.exe`) replaces the Win32 window
(`MIDI++/`, `x64\Release\MIDI++.exe`); both share `PlaybackCore` through
`ShellEngine`. Nothing the original had is optional (`SHELL-GAPS.md`).

## Seats

- Panel seat owns `ui/`, including `ShellEngine::Action` and `EngineSnapshot`.
- Claude owns `MIDI++/`, `tests/`, `tools/`, specs, and engine seams in `ui/`.
- Owner-approved exceptions, not precedents: Claude's Convert popover
  (`DrawConvert`) and the hand-drawn `SettingRadio`, both in `ui/Panels.cpp`,
  2026-09-15.

## Decisions made, do not reopen

- Velocity stays on a modifier (`VELOCITY_MODIFIER`); the tap is four events
  in one `SendInput` call.
- Injection never shares a thread with the message loop (`HANDOFF.md` section 4).
- Legit mode applies at dispatch, never at parse time (`LEGIT-MODE.md`).
- Timing numbers stop at the keyboard hook; never call them end-to-end latency.
- A skin chooses colour only; every skin is 1090 x 635 collapsed.
- Six built-in velocity curves, the sixth being S-Curve (`VELOCITY-CURVES.md`).
- MP3 to MIDI is a Python sidecar, never in process, bundled with a release;
  the bundle pins what a real conversion loads, CPU PyTorch only.
- YouTube links use a signed-in session from `signin.py`; no third-party
  download service. The app never reads a browser's cookies itself.
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.
- One branch. New work goes in a worktree off it and merges back; a merged
  branch is deleted, locally and on `origin`.

## Relevant files

- `tools/make-release.ps1`: builds, stages `converter\` beside the exe, checks
  it on a bare PATH with an mp3, zips. Needs `py -3.12` (or `-Python`).
- `tools/mp3-to-midi/requirements.txt`: the pinned bundle; its header says
  how the list was derived and how to re-derive it after a bump.
- `tests/RenderTests.cpp`: the `minimum` variant captures the 900 x 580 floor.
- `ui/Panels.cpp`: `SettingRadio` and `SettingSwitch` are the hand-drawn
  controls; `DrawSettings` holds the switch descriptions.

## Verified facts

- A running `MIDIShell.exe` blocks the shell link step (LNK1168).
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a click.
- PowerShell 5.1: never redirect a native exe with `2>&1` or `*>` under
  `$ErrorActionPreference = 'Stop'` (`Invoke-Capture` in `make-release.ps1`
  is the safe way); an inline `python -c` loses its double quotes; and
  `Get-ChildItem -Include` with `-LiteralPath` matches every file.
- Run MSBuild from PowerShell: Git Bash rewrites `/p:` switches as paths.
- pydub reads wav itself and every other format through ffprobe, so a
  converter check must use an mp3.
- torch's DLLs import msvcp140, msvcp140_atomic_wait and vcruntime140_threads,
  which the embeddable Python lacks; the bundle ships the VC143 redist beside
  python.exe and the script proves they load from there.
- ImGui's `RadioButton` is a disc the height of a text field whose edge reads
  as a polygon at 125%; hand-drawn circles with auto segments do not.

## Work completed, 2026-09-15

- Release bundle: embeddable Python 3.12.10, 40 pinned packages, FFmpeg and
  ffprobe 9.0.1, Deno 2.9.6, licences under `converter\licenses\`.
- `v0.2.0-test` shipped without ffprobe and converted only wav; superseded.
- `v0.2.1-test` published on K-Alexandru/MIDIPlusPlus-testing from `6526cac`:
  zip 426 MB, SHA256 `7903889551817ABDBEE57DD00DC52881CF7D7FCB66936210C1A808C27454759C`.
- MIDI output and theme radios redrawn as 16px rings with a dot.
- `ShellTests` covers the shipped converter layout; `RenderTests` captures
  the smallest window.

## Unresolved

- **Owner, 2026-09-15, not yet decided who does it:** Settings scrolls too
  long; cut descriptions that restate their switch (Solo piano tracks on
  load, Shuffle Play, Always on top) and fold Wooting, timing and About under
  closed headers. At 900 x 580 the Tracks panel shows half a row and the
  velocity combo overlaps its chevron (`skin-0-125-minimum.png`).
- **Panel seat, not started:** `PROMPT-S-CURVE-AND-SWITCHES.md`.
- **Needs the owner at the keyboard:** Convert with a file and a link in
  `v0.2.1-test`, delivery into a game, Wooting feel, two devices, MIDI output
  into a synth, live curve reconnection, a mixed-DPI move.
- Links from sites that refuse yt-dlp's default client (Wikimedia, 403).

## Validation actually run

- `6526cac`: shell built; `ShellTests.exe` 35 PASS, 0 FAIL; `RenderTests.exe`
  260 PASS; `make-release.ps1` swept 334 binaries, imported the converter on
  a bare PATH, converted an mp3, zipped; the staged bundle converted a
  YouTube link with no sign-in in 11 s. `9141886`: `RenderTests.exe` 280 PASS.
- Not run: parity mutations, native and latency tests, `signin.py`, the shell live.

## Build and test

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'ui\MIDIShell.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& .\tests\run-shell-tests.ps1 -Render
& .\tests\run-shell-parity-mutations.ps1
& .\tools\make-release.ps1
```

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus; `upstream` is Zephkek/MIDIPlusPlus.
  `main` stays at `e37ba7e`. Pushing without asking is authorized.
- The main checkout `D:\Dev\MIDIPlusPlus-modded` is on this branch, pushed,
  clean apart from the rebuilt `x64\Release\MIDI++.exe` and the owner's
  `x64\Release\midi\`. `D:\Dev\mpp-panels` is the panel seat's; leave it.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

With the owner's go-ahead for Claude to work `ui/`: in a worktree, make the
transport panel one row shorter by putting the F-key hints on the title row,
give the collapsed velocity combo a floor width from its longest preset name
and let Sustain cutoff shrink first, cut the three restating descriptions,
then compare the `minimum` and `settings` captures before and after.
