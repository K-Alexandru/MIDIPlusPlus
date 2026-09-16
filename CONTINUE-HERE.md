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
- Owner-approved exceptions on 2026-09-15, not precedents: the Convert
  popover, `SettingRadio`, the transport title row, the collapsed velocity
  row, the Settings trims, the window floor, and the whole of
  `PROMPT-S-CURVE-AND-SWITCHES.md` (S-Curve, two switches, the Export menu).

## Decisions made, do not reopen

- Velocity stays on a modifier (`VELOCITY_MODIFIER`); the tap is four events
  in one `SendInput` call.
- Injection never shares a thread with the message loop (`HANDOFF.md` section 4).
- Legit mode applies at dispatch, never at parse time (`LEGIT-MODE.md`).
- Timing numbers stop at the keyboard hook; never call them end-to-end latency.
- A skin chooses colour only; every skin is 1090 x 635 collapsed.
- The window floor is 900 x 610, the design height. Fit the width, not the height.
- Six built-in velocity curves, the sixth being S-Curve. Its table is the
  inverse of the R5 "radiant grand" values, because the R5 editor drew a
  table as output per step while the engine reads thresholds; the shell now
  draws the S the owner tuned (`SHELL-GAPS.md`, "Pro, now S-Curve").
- MP3 to MIDI is a Python sidecar, never in process, bundled with a release.
- YouTube links use a signed-in session from `signin.py`; no third-party
  download service. The app never reads a browser's cookies itself.
- Sheet image export is the coloured page printed from a browser; a section
  transposition is named by time, since the shell renders no sheet.
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.
- One branch. New work goes in a worktree off it and merges back; a merged
  branch is deleted, locally and on `origin`. The owner confirms each publish.

## Relevant files

- `tools/make-release.ps1`: builds, stages `converter\` beside the exe, zips.
- `ui/Panels.cpp`: `DrawSettings`, `DrawSheetStyle`, `DrawConvert`.
- `ui/ShellEngine.cpp`: `DetectDrums`/`AutoTranspose` fall into `Load`;
  `CopyStyledSheet`/`SaveSheetHtml`; `SheetStyle*` helpers at the top.

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

## Work completed, 2026-09-15

- `v0.2.2-test` published on K-Alexandru/MIDIPlusPlus-testing from `f9f0687`.
- S-Curve inverted so it draws and plays as the S the owner tuned; pencil
  shadow fixed at the generator; mockup renamed Pro to S-Curve, recaptured.
- Settings switches for drum detection and auto-transpose, with reload.
- Export menu: Copy styled sheet, Save coloured sheet, Sheet style popover
  with every `sheet::StyleOptions` field and per-section transposition,
  About credit for midi-converter. All under the "sheet" test group.

## Unresolved

- **Owner to look at live:** the S-Curve graph and how it plays now that the
  response is the S (R5 played its inverse); Settings length; the smallest
  window; Convert with a file and a link; the Sheet style popover.
- **Owner to answer:** the R5 config also holds "s_curve smoth"; S-Curve is
  "radiant grand" per the 09-04 and 09-11 decisions. Say if the other was meant.
- **Needs the owner at the keyboard:** delivery into a game, Wooting feel,
  two devices, MIDI output into a synth, live curve reconnection, a mixed-DPI
  move. The Wikimedia 403 is fixed by a user-agent retry in `convert.py`.
- `build\release\MIDIPlusPlus-test-build.zip` is staged for `v0.2.3-test`;
  the upload waits on the owner's confirmation.

## Validation actually run

- This commit: `ShellTests.exe` all PASS; `RenderTests.exe` 320 PASS across
  16 scenarios, 4 skins, 5 DPI passes; captures for `sheet-style`,
  `settings-switches` and `export` inspected at 100%, the pencil at 200%.
- `run-shell-parity-mutations.ps1`: 24 of 24 killed, baselines passed.
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

Publish `v0.2.3-test` with `tools\make-release.ps1` so the owner can judge
the S-Curve in game, and ask the two owner questions above. Then
`gh issue list --repo K-Alexandru/MIDIPlusPlus-testing` and work the first
report in a worktree off this branch.
