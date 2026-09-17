# Start here

Updated 2026-09-16 on `claude/consolidate-2026-09-15`, the one branch. Read
`HANDOFF.md` only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

## Goal

One Windows app that does everything the original MIDI++ window did and more.
The ImGui shell (`ui/`, `build\shell\MIDIShell.exe`) replaces the Win32 window
(`MIDI++/`); both share `PlaybackCore` through `ShellEngine`. Nothing is optional.

## Seats

- Panel seat owns `ui/`, including `ShellEngine::Action` and `EngineSnapshot`.
  Claude owns `MIDI++/`, `tests/`, `tools/`, specs, and engine seams in `ui/`.
- Owner-asked `ui/` exceptions, not precedents: listed in the passes below.

## Decisions made, do not reopen

- Velocity stays on a modifier; the tap is four events in one `SendInput`.
  Injection never shares a thread with the message loop (`HANDOFF.md`
  section 4). Legit mode applies at dispatch (`LEGIT-MODE.md`).
- Timing numbers stop at the keyboard hook; never call them end-to-end latency.
- A skin chooses colour only; 1090 x 635 collapsed, floor 900 x 610.
- Six built-in curves; S-Curve's engine table is the inverse of the R5 values.
- MP3 to MIDI is a Python sidecar, never in process. YouTube links use a
  signed-in session from `signin.py`; the app never reads browser cookies.
- Sheet settings live in the editor page alone; the app stores none. The page
  is `%TEMP%\MIDI++ sheets\<stem>.html`, opened by the panel, saved from the
  page. Its script is a translation of `sheet::Style`, held by the parity test.
- One transposition per chord; `BestSections` may change it part-way, paying a
  switch cost, and the sheet says "Transpose by" there. A region is Transpose
  (announced) or Notes shifted (silent): JSON `[from, to, semitones, kind]`.
- Sheet files go under the sheets folder (`SHELL_SHEETS_FOLDER`, default a
  "<MIDI folder> sheets" sibling) in the MIDI folder's own sub-folders. The
  style is a saved editor page (`SHELL_SHEET_STYLE_PAGE`), read at each save.
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.
- One branch: work in a worktree off it, merge back, delete the merged branch.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- `MIDI++/SheetExport.hpp`: `BestSections`, `Style`. `MIDI++/SheetPage.hpp`:
  `ToEditorHtml`, the page's script (`SHEET-CORE` is DOM-free for node).
  `MIDI++/SheetImage.hpp`: `SavePng`.
- `ui/ShellEngine.cpp`: `StyleFromPage`, `SheetTarget`, `WriteSheetFiles`,
  `PageForFile`, the `SaveLibrarySheets` thread. `ui/Panels.cpp`: Export menu.
- `tests/ShellTests.cpp`: `SheetSectionTests`, `SheetFilesTests`, the parity fixture.

## Verified facts

- A running `MIDIShell.exe` blocks the shell link step (LNK1168) and, run
  from `build\release\MIDIPlusPlus`, blocks `make-release.ps1` too.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a click.
  A DPI-unaware `PrintWindow` is the shell scaled down 20%; set awareness first.
- PowerShell 5.1: never redirect a native exe with `2>&1` or `*>` under
  `$ErrorActionPreference = 'Stop'`; run `run-shell-tests.ps1` bare.
- Actions before `CurveSelect` in the enum are dropped unless the generation
  matches; a new engine's first snapshot is blank, so tests `Await` the config.
- Run MSBuild from PowerShell: Git Bash rewrites `/p:` switches as paths.

## Work completed, 2026-09-16

- Multi-transpose in the editor: "Change transposition part-way" under Find
  the best transposition, with Switch cost, Shortest section, Rest before a
  switch and Search range; Sections lists runs, Keep makes them the user's; a
  selection offers Transpose and Shift notes. Parity and three mutations hold it.
- Sheet files (owner-asked `ui/` exception). The page has Save image (SVG
  foreignObject to a canvas at 2x); both saves use `showSaveFilePicker` with a
  remembered folder where the browser has it. Export gained Save sheet files,
  Save sheets for the whole library (own thread, Stop, progress and failures
  in the status and log), Image/Text/Editor page switches, Sheets folder and
  Style from a saved page. `SheetFilesTests` and two mutations cover it.

## Unresolved

- **Owner to test on the `bb73374` build:** sections on a real file (defaults
  12 notes, 8 s, 250 ms, 12 semitones); Save image from a page opened off disk
  (the picker is untested from `file://`); the library save over 3325 files.
- **Owner's call:** a green pill's top highlight shows faintly inside its border.
- **Needs the owner at the keyboard:** game delivery, Wooting, two devices,
  MIDI out, live curve reconnection, mixed DPI, the 900 x 610 clamp.

## Validation actually run

- `ShellTests.exe` all PASS; sheet page parity 11 of 11; the shell built. The
  editor page ran in the built-in browser: sections, Keep, Transpose, Shift
  notes and the image path, no console errors. A driver ran the library save
  over three real MIDIs in two artist folders and the PNGs read right.
- Parity mutations 27 of 27 killed, in the worktree, sources restored.
- The state pills at 125%, in the captures of all four skins and the live
  window at physical pixels, 4x: clean. `make-release.ps1` packaged `bb73374`.
- Not run: `RenderTests`, native and latency tests, `signin.py`, the shell live.

## Build and test

```powershell
& .\tests\run-shell-tests.ps1 -Render
& .\tests\run-shell-parity-mutations.ps1
& .\tools\make-release.ps1
```

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus; `upstream` is Zephkek/MIDIPlusPlus.
  `main` stays at `e37ba7e`. Pushing without asking is authorized.
- The main checkout `D:\Dev\MIDIPlusPlus-modded` is on this branch, pushed,
  clean apart from the owner's `x64\Release\MIDI++.exe` and `x64\Release\midi\`.
  `D:\Dev\mpp-panels` is the panel seat's; leave it.
- Test build from `bb73374`: `build\release\MIDIPlusPlus-test-build.zip`
  (426 MB) and the staged folder, which the owner runs the shell from.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Ask the owner what they found on the `bb73374` test build, in the sheet
editor, Save image and the library save, and fix the first thing they name in
a worktree off this branch, then repackage with `.\tools\make-release.ps1`.
