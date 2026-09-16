# Start here

Updated 2026-09-16 on `claude/consolidate-2026-09-15`, the one branch every
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
- Owner-approved exceptions on 2026-09-15, not precedents: the Convert popover,
  Settings trims, window floor, UI review fixes, `PROMPT-S-CURVE-AND-SWITCHES.md`.

## Decisions made, do not reopen

- Velocity stays on a modifier (`VELOCITY_MODIFIER`); the tap is four events
  in one `SendInput` call.
- Injection never shares a thread with the message loop (`HANDOFF.md` section 4).
- Legit mode applies at dispatch, never at parse time (`LEGIT-MODE.md`).
- Timing numbers stop at the keyboard hook; never call them end-to-end latency.
- A skin chooses colour only; every skin is 1090 x 635 collapsed.
- The window floor is 900 x 610, the design height. Fit the width, not the height.
- Six built-in velocity curves, the sixth being S-Curve: the R5 "radiant
  grand" values read as a response, so the engine table is their inverse
  and the shell draws the S the owner tuned (`SHELL-GAPS.md`, "Pro, now
  S-Curve"). Owner confirmed 2026-09-15; R5's "s_curve smoth" stays out.
- MP3 to MIDI is a Python sidecar, never in process, bundled with a release.
- YouTube links use a signed-in session from `signin.py`; no third-party
  download service. The app never reads a browser's cookies itself.
- Sheet image export is the coloured page printed from a browser; a section
  transposition is named by time, since the shell renders no sheet.
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.
- One branch. New work goes in a worktree off it and merges back; a merged
  branch is deleted, locally and on `origin`.
- Test builds stay on this PC (owner, 2026-09-16): `make-release.ps1` zips to
  `build\release\` and the reply gives the paths. No GitHub release uploads.

## Relevant files

- `tools/make-release.ps1`: builds, stages `converter\` beside the exe, zips;
  `tools/release-README.txt` is the tester README inside the zip.
- `ui/Panels.cpp`: `DrawSettings`, `DrawSheetStyle`, `DrawConvert`, `DrawLog`;
  `ui/ShellEngine.cpp`: the switches fall into `Load`, `SheetStyle*` helpers.

## Verified facts

- A running `MIDIShell.exe` blocks the shell link step (LNK1168).
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a click.
- PowerShell 5.1: never redirect a native exe with `2>&1` or `*>` under
  `$ErrorActionPreference = 'Stop'`; that includes wrapping
  `run-shell-tests.ps1` itself. Run it bare.
- Actions before `CurveSelect` in the enum are dropped unless
  `Command::generation` matches, apart from the listed exemptions; a new
  engine's first snapshot is blank, so tests `Await` the parsed config.
- Run MSBuild from PowerShell: Git Bash rewrites `/p:` switches as paths.

## Work completed, 2026-09-15 and 16

- `v0.2.3-test` (`b8281cb`) and `v0.2.4-test` (`9e5910a`) published on
  K-Alexandru/MIDIPlusPlus-testing. The latter's zip is 426 MB, SHA256
  `2389B27EC39C0ABF570881D363524AFBC7A909D34D7E9CCDDAFF9F3ECB32EDF2`.
- S-Curve inverted so it draws and plays as tuned; pencil shadow fixed at
  the icon generator; mockup says S-Curve and `docs/design` is recaptured.
- Settings switches for drum detection and auto-transpose, reloading the
  open file in place.
- Export menu: Copy styled sheet, Save coloured sheet, Sheet style popover
  with every `sheet::StyleOptions` field and per-section transposition.
- The Wikimedia 403: a refused direct link is retried under the app's user agent.
- Review fixes: config save guard is a parse flag, separator cut on a UTF-8
  boundary, all six presets fit the list, log window skinned with an eraser
  for Clear, key-mapping and autovol render scenarios, page titled after
  its file. The owner's 2026-09-07 UI pass is closed in `SHELL-GAPS.md`.

## Unresolved

- **Owner to look at live on `v0.2.4-test`:** S-Curve in game (R5 played its
  inverse); Settings length; the smallest window; Convert; Sheet style.
- **Needs the owner at the keyboard:** delivery into a game, Wooting feel,
  two devices, MIDI output into a synth, live curve reconnection, a mixed-DPI
  move, the 900 x 610 clamp on a real window.

## Validation actually run

- `9e5910a`: `ShellTests.exe` all PASS; `RenderTests.exe` 360 PASS over 18
  scenarios; every new capture inspected at 100%, the pencil at 200%.
- `run-shell-parity-mutations.ps1` at `6f026c4`: 24 of 24 killed.
- `make-release.ps1` at `9e5910a`: converter check passed; the asset matches the zip.
- Not run: native and latency tests, `signin.py`, the shell live.

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
  clean apart from the owner's rebuilt `x64\Release\MIDI++.exe` and
  `x64\Release\midi\`. `D:\Dev\mpp-panels` is the panel seat's; leave it.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Run `gh issue list --repo K-Alexandru/MIDIPlusPlus-testing` and work the
first tester report on `v0.2.4-test` in a worktree off this branch. With no
reports, ask the owner for the cursor and run `tests\run-native-tests.ps1`
to prove the 900 x 610 clamp on the real window.
