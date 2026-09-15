# Start here

Updated 2026-09-15 on `claude/consolidate-2026-09-15`, the one branch every
earlier branch was folded into. Then read `HANDOFF.md` only where this file
points, `SHELL-GAPS.md` (what the shell owes) and `SEATS.md` (who does what).

## Goal

One Windows app that does everything the original MIDI++ window did and more.
The ImGui shell (`ui/`, `build\shell\MIDIShell.exe`) replaces the Win32 window
(`MIDI++/`, `x64\Release\MIDI++.exe`); both share `PlaybackCore` through
`ShellEngine`. Nothing the original had is optional (`SHELL-GAPS.md`).
`v0.2.2-test` is with testers; the current work is what they report.

## Seats

- Panel seat owns `ui/`, including `ShellEngine::Action` and `EngineSnapshot`.
- Claude owns `MIDI++/`, `tests/`, `tools/`, specs, and engine seams in `ui/`.
- Owner-approved exceptions on 2026-09-15, not precedents: the Convert
  popover, `SettingRadio`, the transport title row, the collapsed velocity
  row, the Settings trims (`ui/Panels.cpp`) and the window floor (`ui/Shell.cpp`).

## Decisions made, do not reopen

- Velocity stays on a modifier (`VELOCITY_MODIFIER`); the tap is four events
  in one `SendInput` call.
- Injection never shares a thread with the message loop (`HANDOFF.md` section 4).
- Legit mode applies at dispatch, never at parse time (`LEGIT-MODE.md`).
- Timing numbers stop at the keyboard hook; never call them end-to-end latency.
- A skin chooses colour only; every skin is 1090 x 635 collapsed.
- The window floor is 900 x 610, the design height: below it the Tracks panel
  is one row and nothing has a shorter form. Fit the width, not the height.
- Six built-in velocity curves, the sixth being S-Curve (`VELOCITY-CURVES.md`).
- MP3 to MIDI is a Python sidecar, never in process, bundled with a release;
  the bundle pins what a real conversion loads, CPU PyTorch only.
- YouTube links use a signed-in session from `signin.py`; no third-party
  download service. The app never reads a browser's cookies itself.
- Settings text says only what a label cannot; a switch may have no description.
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.
- One branch. New work goes in a worktree off it and merges back; a merged
  branch is deleted, locally and on `origin`. Publishing a test build is a
  separate step the owner confirms.

## Relevant files

- `tools/make-release.ps1`: builds, stages `converter\` beside the exe, checks
  it on a bare PATH with an mp3, zips. Needs `py -3.12` (or `-Python`).
- `tools/mp3-to-midi/requirements.txt`: the pinned bundle; its header says
  how the list was derived and how to re-derive it after a bump.
- `tests/RenderTests.cpp`: the `minimum` variant captures the 900 x 610 floor.
- `ui/Panels.cpp`: `SettingRadio`, `SettingSwitch`, `DrawSettings`, `DrawConvert`.

## Verified facts

- A running `MIDIShell.exe` blocks the shell link step (LNK1168).
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a click.
- PowerShell 5.1: never redirect a native exe with `2>&1` or `*>` under
  `$ErrorActionPreference = 'Stop'` (`Invoke-Capture` in `make-release.ps1`
  is the safe way); an inline `python -c` loses its double quotes; and
  `Get-ChildItem -Include` with `-LiteralPath` matches every file.
- Run MSBuild from PowerShell: Git Bash rewrites `/p:` switches as paths. Run
  `RenderTests.exe` from its folder in bash; a stderr line kills a script.
- pydub needs ffprobe for every format but wav; a converter check must use an mp3.
- torch's DLLs import msvcp140, msvcp140_atomic_wait and vcruntime140_threads,
  which the embeddable Python lacks; the bundle ships the VC143 redist beside
  python.exe and the script proves they load from there.
- YouTube downloaded without a sign-in from the owner's connection on
  2026-09-15 evening; the 2026-09-14 bot check is not constant.

## Work completed, 2026-09-15

- Release bundle: embeddable Python 3.12.10, 40 pinned packages, FFmpeg and
  ffprobe 9.0.1, Deno 2.9.6, licences under `converter\licenses\`.
- `v0.2.0-test` and `v0.2.1-test` published and superseded (the first shipped
  no ffprobe and converted wav only).
- `v0.2.2-test` published on K-Alexandru/MIDIPlusPlus-testing from `f9f0687`:
  zip 426 MB, SHA256 `7D5C6D8EABF8086E2B0BF44F3553DEC578B9D42B338F872EB6C2A66BCD31473A`.
  It carries the redrawn radios, the F-key hints on the transport title row,
  the velocity combo floored at its longest preset name, three restating
  Settings descriptions cut, Keyboard timing folded, and the 900 x 610 floor.

## Unresolved

- **Owner to look at live in `v0.2.2-test`:** Settings length, the smallest
  window, Convert with a file and a link.
- **Panel seat, not started:** `PROMPT-S-CURVE-AND-SWITCHES.md`.
- **Needs the owner at the keyboard:** delivery into a game, Wooting feel,
  two devices, MIDI output into a synth, live curve reconnection, a mixed-DPI
  move. Wikimedia links still 403.
- Tester README and release notes list "Some panels from the old version
  aren't back yet"; `SHELL-GAPS.md` says which.

## Validation actually run

- `6526cac`: `ShellTests.exe` 35 PASS, 0 FAIL; the staged bundle converted an
  mp3 and a YouTube link on a bare PATH.
- `f9f0687`: shell built; `RenderTests.exe` 280 PASS; `make-release.ps1`
  swept 334 binaries, imported the converter on a bare PATH, converted an
  mp3, zipped; the uploaded asset matches the zip.
- Not run: parity mutations, native and latency tests (the 610 clamp on a
  real window is unproven), `signin.py`, the shell live.

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

Run `gh issue list --repo K-Alexandru/MIDIPlusPlus-testing` and work the
first tester report on `v0.2.2-test` in a worktree off this branch. With no
reports, ask the owner for the cursor and run `tests\run-native-tests.ps1`
to prove the 900 x 610 clamp on the real window; then take the Wikimedia
403 by retrying a refused link with yt-dlp's generic extractor.
