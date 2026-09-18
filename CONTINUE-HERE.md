# Start here

Updated 2026-09-18 on `claude/consolidate-2026-09-15`, the one branch. Read
`REVIEW-2026-09-18.md` (open bugs, UI and performance findings), `HANDOFF.md`
only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

## Goal

One Windows app, QuartzMIDI, that does everything the original MIDI++ window
did and more. The ImGui shell (`ui/`, `build\shell\QuartzMIDI.exe`) replaces
the Win32 window (`MIDI++/`); both share `PlaybackCore` through `ShellEngine`.
Nothing is optional. Demo v2 is the build the owner shares with testers.

## Seats

- Panel seat owns `ui/`, including `ShellEngine::Action` and `EngineSnapshot`.
  Claude owns `MIDI++/`, `tests/`, `tools/`, specs, and engine seams in `ui/`.
  Every `ui/` change below was owner-asked; none is a precedent.

## Decisions made, do not reopen

- The product name is QuartzMIDI: the window title, the exe, the icon, the
  package folder and zip, the sheets folders. MIDI++ survives only in source
  tree names (`MIDI++/`, `MIDIShell.vcxproj`, the window class) and credits.
- Nothing sent out carries an account name, an assistant name or a working
  doc; `tools/make-source.py` zips only `ui/`, `MIDI++/`, `third_party/`,
  config and LICENSE, scrubbed. The library save asks before it writes.
- UI copy is the owner's voice, `HANDOFF.md` section 15 and this rule:
  no explanatory text anywhere, not even why a control is disabled. If a
  control seems to need a sentence, redesign it. Help, later, is one button.
- The caption follows the skin through DWM (`ApplyCaption` in `ui/Shell.cpp`).
- Key Mapping never opens on its own; `keyMappingOpen` is not saved.
- Solo Piano is one toggle (`SoloPianoApplied`); a second click is Unmute All.
  A part named Flute or Drums with no program change is not piano and its
  Instrument column says Flute (`DescribeTracks`); an explicit program wins.
- Skins are Blue, Blue Dark, Orange and Orange Dark, colour only. The window
  opens 940 x 600, floor 884 x 560 (`Panels::MinimumSize`); mini is fixed at
  528 x 164 Live and 528 x 256 Autoplay.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- `ui/Panels.cpp`: `DesiredSize`, `DrawMini`, the strip in `Draw`, the Export
  menu, the state pills. `MIDI++/SheetPage.hpp`: the editor page.
- `ui/Shell.cpp`: `ApplyCaption`, the mini and maximize handling in the loop.
- `tools/make-release.ps1`, `tools/make-source.py`, `tools/gen-icon.py`.

## Verified facts

- A running `QuartzMIDI.exe` blocks the shell link step (LNK1168) and, run
  from `build\release\QuartzMIDI`, blocks `make-release.ps1`. Ask the owner
  to close it; never kill it.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a
  click. `Start-NativeShell` waits for the title `QuartzMIDI`.
- PowerShell 5.1: never redirect a native exe with `2>&1` or `*>` under
  `$ErrorActionPreference = 'Stop'`; run `run-shell-tests.ps1` bare.
- A `RenderTests` scenario opens a popup through a `Panels` flag, as
  `openConvert` does; its click positions follow the strip and Files width.
- `build\shell\config.json` is rewritten by every run of the built shell. A
  release copies the tracked `x64\Release\config.json` and refuses if it is
  modified. `/d1trimfile` keeps the repository path out of the exe.

## Work completed

- 2026-09-16: the rename, caption, icon, Orange skins, keycap legend, Solo
  Piano toggle (owner confirmed), Export relabelled with a confirmed library
  save, `tools/make-source.py`, the copy pass down to no explanatory text.
- 2026-09-17, owner-asked, both windows smaller: mini is the pill row wide
  with no resize or maximize box; full has a one-row strip, the sheet result
  in the status bar, and Files 240 to 336 after the right column's 600.
- 2026-09-18, a bug pass: a custom curve played the curve before it, a curve
  change stuck a held live key, sheets inherited the playback speed, a failed
  config save pinned a core. Fixed, the first three under mutation-checked
  tests. Everything else found is in `REVIEW-2026-09-18.md`, unfixed.

## Unresolved

- **Owner to choose from `REVIEW-2026-09-18.md`:** the `ui/` items are the
  panel seat's or owner-asked; demo v2 predates the four fixes.
- **Owner to test the `3aeea33` build:** the velocity editor while maximized;
  Miphas Court's Flutes reading Flute.
- **Owner to read the text:** Settings, pills, Export, the sheet editor page.
- **Owner to test:** Solo Piano in mini; sections on a real file; Save image
  from a page opened off disk.
- **Owner at the keyboard:** game delivery, Wooting, two devices, MIDI out,
  live curve reconnection, mixed DPI.

## Validation actually run

- 2026-09-18: `ShellTests.exe`, parity and all 360 render scenarios PASS with
  the fixes; `run-native-tests.ps1` PASS at `9d05e76`, DPI move skipped.
- At `1de8a50`, not repeated since: the package scrub (no names, paths,
  settings or MIDI) and the source zip building on its own.

## Build and test

```powershell
& .\tests\run-shell-tests.ps1 -Render
& .\tools\make-release.ps1
python .\tools\make-source.py
```

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus; `upstream` is Zephkek/MIDIPlusPlus.
  `main` stays at `e37ba7e`. Pushing without asking is authorized.
- The main checkout is on this branch, pushed, clean apart from the owner's
  `x64\Release\MIDI++.exe` and `x64\Release\midi\`; leave `D:\Dev\mpp-panels`.
- Demo v2 from `3aeea33`: `build\release\QuartzMIDI-demo-v2.zip` (426 MB,
  SHA256 `32D81AD90E95F136C4B5206F64962AE5A1E05837BF1E79AA3D765729F7672E01`),
  the staged folder beside it, and `QuartzMIDI-source-3aeea33.zip` (2.4 MB);
  the `1de8a50` and `4e55ecc` source zips are still there.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Ask the owner which `REVIEW-2026-09-18.md` items to take, fix those, then
run `make-release.ps1` and `make-source.py`.
