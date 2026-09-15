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
- `ui/Panels.cpp`: `SettingRadio`, `SettingSwitch`, `DrawSettings`.

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

## Work completed, 2026-09-15

- Release bundle: embeddable Python 3.12.10, 40 pinned packages, FFmpeg and
  ffprobe 9.0.1, Deno 2.9.6, licences under `converter\licenses\`.
- `v0.2.0-test` (no ffprobe, wav only) and `v0.2.1-test` (`6526cac`, fixed,
  radios redrawn as rings) published on K-Alexandru/MIDIPlusPlus-testing;
  both superseded.
- `c5fcc7d`: F-key hints on the transport title row, velocity combo floored
  at its longest preset name, three restating descriptions cut, Keyboard
  timing under a closed header.
- `f9f0687`: window floor raised to 900 x 610, the design height, so two
  track rows always fit; `v0.2.2-test` published from it, zip 426 MB, SHA256
  `7D5C6D8EABF8086E2B0BF44F3553DEC578B9D42B338F872EB6C2A66BCD31473A`.
- `ShellTests` covers the shipped converter layout; `RenderTests` has `minimum`.

## Unresolved

- **Owner to look at live:** the shorter Settings and the smallest window in
  `v0.2.2-test`.
- **Panel seat, not started:** `PROMPT-S-CURVE-AND-SWITCHES.md`.
- **Needs the owner at the keyboard:** Convert in `v0.2.2-test`, delivery
  into a game, Wooting feel, two devices, MIDI output into a synth, live
  curve reconnection, a mixed-DPI move. Wikimedia links still 403.

## Validation actually run

- `6526cac`: shell built; `ShellTests.exe` 35 PASS, 0 FAIL; `RenderTests.exe`
  260 PASS; `make-release.ps1` swept 334 binaries, imported the converter on
  a bare PATH, converted an mp3, zipped; the staged bundle converted a
  YouTube link with no sign-in in 11 s. `f9f0687`: shell built;
  `RenderTests.exe` 280 PASS; `make-release.ps1` passed its checks;
  `ShellTests` not rerun, it compiles neither `Panels.cpp` nor `Shell.cpp`.
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

Run `gh issue list --repo K-Alexandru/MIDIPlusPlus-testing` and work the
first tester report on `v0.2.2-test`. With no reports, run
`tests\run-native-tests.ps1` with the owner's consent (it takes the cursor)
to confirm the 900 x 610 clamp on the real window, then take the Wikimedia
403: retry a refused link with yt-dlp's generic extractor.
