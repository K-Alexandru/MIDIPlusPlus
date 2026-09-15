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
- Owner-approved exception, not a precedent: Claude's Convert popover in
  `ui/Panels.cpp` (`DrawConvert`, 2026-09-15).

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
  it on a bare PATH, zips. Needs `py -3.12` (or `-Python`) to fetch wheels.
- `tools/mp3-to-midi/requirements.txt`: the pinned bundle; its header says
  how the list was derived and how to re-derive it after a bump.
- `MIDI++/AudioToMidi.hpp`: `FindInstall` prefers `converter\python\python.exe`.

## Verified facts

- A running `MIDIShell.exe` blocks the shell link step (LNK1168).
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a click.
- PowerShell 5.1: never redirect a native exe with `2>&1` or `*>` under
  `$ErrorActionPreference = 'Stop'` (`Invoke-Capture` in `make-release.ps1`
  is the safe way); an inline `python -c` loses its double quotes; and
  `Get-ChildItem -Include` with `-LiteralPath` matches every file.
- Run MSBuild from PowerShell: Git Bash rewrites `/p:` switches as paths.
- A real Transkun transcription loads torch, torchaudio, numpy, scipy, sympy,
  mir_eval, pretty_midi, mido, pydub, soxr, moduleconf, tqdm and setuptools,
  never pandas, matplotlib, seaborn, networkx, tensorboard, ncls or sox.
- torch's DLLs import msvcp140, msvcp140_atomic_wait and vcruntime140_threads,
  which the embeddable Python lacks; the bundle ships the VC143 redist beside
  python.exe and the script proves they load from there.

## Work completed, 2026-09-15

- Release bundle: `converter\` with embeddable Python 3.12.10, 40 pinned
  packages, `convert.py`, `signin.py`, FFmpeg 9.0.1 essentials, Deno 2.9.6,
  every licence under `converter\licenses\`; downloads pinned by SHA256 and
  cached in `build\release\downloads`. `convert.py` finds `deno\` beside it.
- `ShellTests` gained the shipped-layout case for `FindInstall`.
- Tester README names Convert and Sign in to YouTube, and credits the converter.
- `v0.2.0-test` published from `d76483d`, then found to convert only wav: it
  shipped no ffprobe, which pydub needs for every other format.
- `2a60a25` bundles ffprobe and checks with an mp3; repackaged zip 426 MB,
  SHA256 `91AF0E66F21562936C4ECF312A78208815D72D51771D72DE643C95B070D2CF0E`.

## Unresolved

- **Panel seat, not started:** the owner hands over
  `PROMPT-S-CURVE-AND-SWITCHES.md`; its three pieces are listed there.
- **Owner, 2026-09-15:** the Convert popover "can be simplified"; ask what
  before redesigning it.
- **Needs the owner at the keyboard:** Convert with a file and a link in the
  fixed build, delivery into a game, Wooting feel, two devices, MIDI output
  into a synth, live curve reconnection, a mixed-DPI move.
- Links from sites that refuse yt-dlp's default client (Wikimedia, 403).

## Validation actually run

- `d76483d`: shell built; `ShellTests.exe` 35 PASS, 0 FAIL. `2a60a25`:
  `make-release.ps1` swept 334 binaries for VC runtime imports, imported
  torch, transkun, yt-dlp and pywebview with PATH cut to Windows, converted a
  3 s mp3, zipped; the staged bundle then converted a YouTube link with no
  sign-in on a bare PATH, 11 s end to end.
- Not run: render tests and parity mutations (no `ui/` or engine change),
  native and latency tests, `signin.py`, the shell live.

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

With the owner's yes, publish the fixed zip in `build\release` as
`v0.2.1-test` on K-Alexandru/MIDIPlusPlus-testing, notes as for `v0.2.0-test`
plus one line saying mp3 and link conversion now work, naming `2a60a25` and
the SHA256 above; then mark `v0.2.0-test` as superseded in its notes.
