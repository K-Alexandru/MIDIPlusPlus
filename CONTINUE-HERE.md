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
  trims, window floor, UI review fixes, and the 2026-09-16 passes below.

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
- A sheet has one transposition per chord. The search (`BestSections`) may
  change it part-way, paying a switch cost, and the sheet says "Transpose by"
  where it changes. A region is either Transpose (sets the reader's
  transposition, announced) or Notes shifted (rewrites the notes, silent);
  regions are JSON `[from, to, semitones, kind]`, three elements meaning shifted.
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.
- One branch: work in a worktree off it, merge back, delete the merged branch.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`
  and the reply gives the paths. No GitHub release uploads.

## Relevant files

- `MIDI++/SheetExport.hpp`: `StyleOptions` (the five `section*` fields),
  `ChordIndices`, `BestSections`, `Style` with its `sections` parameter.
- `MIDI++/SheetPage.hpp`: `Region::Kind`, `ToEditorHtml`, the page's CSS, its
  script in two halves (`SHEET-CORE` is DOM-free for node), `PageJson`.
- `tests/ShellTests.cpp`: `SheetSectionTests`, `WriteSheetPageParityFixture`.

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

- Export is Copy sheet and Open sheet editor; the page holds every style
  setting. Settings checkboxes and the velocity graph tidied.
- Multi-transpose in the sheet editor. Under Find the best transposition,
  "Change transposition part-way" runs a dynamic programme over chords: the
  original's score per chord less Switch cost (notes), with Shortest section,
  Rest before a switch and Search range. Sections lists every run; Keep these
  sections makes found runs the user's. A selection offers Transpose
  (announced, over the search) and Shift notes (silent). The C++ `Style` has
  the same search, so the parity fixture covers it; three mutations guard it.

## Unresolved

- **Owner to look at live:** the sheet editor's sections from a real file
  (defaults are 12 notes, 8 s, 250 ms, 12 semitones); Settings; the graph.
- **Needs the owner at the keyboard:** game delivery, Wooting, two devices,
  MIDI out to a synth, live curve reconnection, mixed DPI, the 900 x 610 clamp.

## Validation actually run

- `ShellTests.exe` all PASS including `SheetSectionTests`; sheet page parity
  11 of 11; the shell built. The page ran in the built-in browser on the
  parity fixture's score with no console errors: the search split at zero
  cost and stayed whole at the defaults; Keep, Transpose and Shift notes work.
- Parity mutations 25 of 25 killed, in the worktree, sources restored.
- Not run: `RenderTests`, native and latency tests, `signin.py`, the shell live.

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

Ask the owner what they found in the sheet editor's sections on a real file,
and fix the first thing they name in a worktree off this branch. With nothing
named, run `.\tools\make-release.ps1` so a test build carries the sections.
