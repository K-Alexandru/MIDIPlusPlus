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
- Owner-approved exceptions, not precedents: the Convert popover, Settings
  trims, window floor, UI review fixes, and the 2026-09-16 pass below.

## Decisions made, do not reopen

- Velocity stays on a modifier (`VELOCITY_MODIFIER`); the tap is four events
  in one `SendInput` call. Injection never shares a thread with the message
  loop (`HANDOFF.md` section 4). Legit mode applies at dispatch (`LEGIT-MODE.md`).
- Timing numbers stop at the keyboard hook; never call them end-to-end latency.
- A skin chooses colour only; every skin is 1090 x 635 collapsed. The window
  floor is 900 x 610, the design height. Fit the width, not the height.
- Six built-in velocity curves, the sixth being S-Curve: the engine table is
  the inverse of the R5 values and the shell draws the S the owner tuned.
- MP3 to MIDI is a Python sidecar, never in process, bundled with a release.
  YouTube links use a signed-in session from `signin.py`; the app never reads
  a browser's cookies itself.
- Sheet settings live in one place, the editor page in the browser, and the
  app stores none: Export is Copy sheet and Open sheet editor, nothing else.
  The page is `%TEMP%\MIDI++ sheets\<stem>.html`, opened by the panel, saved
  from the page, never a file beside the MIDI. Its script is a translation of
  `sheet::Style`; the parity test holds the two together.
- The velocity graph carries a grid, one hairline for the unchanged
  response, the step fill, the histogram and the curve. No dashed lines, no
  markers at rest; the played-velocity guides show only while an anchor drags.
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.
- One branch: work in a worktree off it, merge back, delete the merged branch.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`
  and the reply gives the paths. No GitHub release uploads.

## Relevant files

- `MIDI++/SheetPage.hpp`: `ToEditorHtml`, the page's CSS, its script in two
  halves (`SHEET-CORE` is DOM-free for node), `PageJson`.
- `ui/Panels.cpp`: `SettingRadio`, `SettingCheck`, `DrawVelocity`; the
  Export menu and the browser launch in `Draw`.

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

## Work completed, 2026-09-16

- Export is two entries: Copy sheet and Open sheet editor. The editor page
  holds every style setting, remembered in the browser, with selection
  transposition, Copy, Save page, Print and Reset settings. The in-app Sheet
  style popover, `SHELL_SHEET_STYLE` and the region actions are removed.
  `SheetMenuTests` requires the temp path and no file beside the MIDI; the
  parity fixture covers nine sheets.
- Settings: the Keystrokes/MIDI radios no longer draw a hover box around the
  label; Midi2Key, Measure keyboard timing and Whole playlist are
  `SettingCheck`, a rounded square with the Lucide check, green when on.
- Velocity graph: dashed diagonal, dashed snap lines and their triangles,
  the staircase outline and the hairline gaps in the step fill are gone.
- Log: MMCSS refusal is one note. Mockup graph matched, `docs/design` recaptured.

## Unresolved

- **Owner to look at live:** the sheet editor from a real file; Settings; the graph.
- **Needs the owner at the keyboard:** game delivery, Wooting, two devices,
  MIDI out to a synth, live curve reconnection, mixed DPI, the 900 x 610 clamp.

## Validation actually run

- `ShellTests.exe` all PASS; sheet page parity 9 of 9; `RenderTests.exe`
  every scenario PASS at 100 to 200%; the page opened in the built-in browser
  with live transposition and a section from a selection, no console errors.
- Mutations 23 of 23 killed; `make-release.ps1` zipped `ef4aad6` to `build\release\`.
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
- The main checkout `D:\Dev\MIDIPlusPlus-modded` is on this branch at
  `97752fd`, pushed, clean apart from the owner's rebuilt `x64\Release\MIDI++.exe`
  and `x64\Release\midi\`. `D:\Dev\mpp-panels` is the panel seat's; leave it.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Ask the owner what they found in the sheet editor, Settings and the graph on
the `ef4aad6` build, and fix the first thing they name in a worktree off this
branch. With nothing named, run `gh issue list --repo
K-Alexandru/MIDIPlusPlus-testing` and work the first tester report.
