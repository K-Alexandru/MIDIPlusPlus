# Start here

Updated 2026-09-16 on `claude/consolidate-2026-09-15`, the one branch. Read
`HANDOFF.md` only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

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
  doc: the converter's user agent is plain, and `tools/make-source.py` zips
  only `ui/`, `MIDI++/`, `third_party/`, config and LICENSE, scrubbed and
  scanned. The library save asks before it writes.
- UI copy is the owner's voice, `HANDOFF.md` section 15 and this rule:
  no explanatory text anywhere, not even why a control is disabled. If a
  control seems to need a sentence, redesign it. Help, later, is one button.
- The caption follows the skin through DWM (`ApplyCaption` in `ui/Shell.cpp`).
- Key Mapping never opens on its own; `keyMappingOpen` is not saved.
- Solo Piano is one toggle (`SoloPianoApplied`); a second click is Unmute All.
  A part named Flute, Strings, Drums and so on with no program change on its
  channel is not piano (`DescribeTracks`); an explicit program always wins.
- Skins are Blue, Blue Dark, Orange and Orange Dark. A skin chooses colour
  only; 1090 x 635 collapsed, floor 900 x 610.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- `ui/Panels.cpp`: the Export menu and its confirmation, the settings
  descriptions, the state pills. `MIDI++/SheetPage.hpp`: the editor page.
- `tools/release-README.txt`: the tester notes.
- `ui/Shell.cpp`: `ApplyCaption`, the window class icon. `ui/TrackModel.*`.
- `tools/make-release.ps1`, `tools/make-source.py`, `tools/gen-icon.py`.

## Verified facts

- A running `QuartzMIDI.exe` blocks the shell link step (LNK1168) and, run
  from `build\release\QuartzMIDI`, blocks `make-release.ps1`. Ask the owner
  to close it; never kill it.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a
  click. `Start-NativeShell` waits for the title `QuartzMIDI`.
- PowerShell 5.1: never redirect a native exe with `2>&1` or `*>` under
  `$ErrorActionPreference = 'Stop'`; run `run-shell-tests.ps1` bare.
- `RenderTests` mode 17 `library-save` captures the confirmation; a scenario
  opens a popup through a `Panels` flag, as `openConvert` does.

## Work completed, 2026-09-16

- Rename to QuartzMIDI; caption from the skin; the icon; Key Mapping closed
  at start; Orange skins; hotkey legend as keycaps.
- Solo Piano as one toggle with name-based piano detection; the owner
  confirmed it on Miphas Court (five Flutes, one MIDI Region).
- Export menu items relabelled with tooltips; the library save confirms
  first; the converter user agent no longer names the repository.
- `tools/make-source.py` and the clean source zip.
- The copy pass, three cuts, each sent back by the owner as not enough:
  no tooltip, description or note explains a control any more, not even the
  reason a control is disabled. Only results are reported ("Every track is
  piano."). OutRange became "Fold out-of-range notes onto the keys".

## Unresolved

- **Owner to read the `1542a14` build's text:** Settings, the state pills,
  the Export menu and its confirmation, the sheet editor page.
- **Owner to test:** the Solo Piano toggle in the mini window; sections on a
  real file; Save image from a page opened off disk.
- **Needs the owner at the keyboard:** game delivery, Wooting, two devices,
  MIDI out, live curve reconnection, mixed DPI, the 900 x 610 clamp.

## Validation actually run

- `ShellTests.exe` all PASS, parity passed, `RenderTests.exe` all PASS at
  100 to 200% in four skins at `1542a14`; the settings capture was read.
- The source zip's scan found nothing at `1542a14`.

## Build and test

```powershell
& .\tests\run-shell-tests.ps1 -Render
& .\tools\make-release.ps1
python .\tools\make-source.py
```

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus; `upstream` is Zephkek/MIDIPlusPlus.
  `main` stays at `e37ba7e`. Pushing without asking is authorized.
- The main checkout `D:\Dev\MIDIPlusPlus-modded` is on this branch, pushed,
  clean apart from the owner's `x64\Release\MIDI++.exe` and `x64\Release\midi\`;
  `D:\Dev\mpp-panels` is the panel seat's, leave it.
- Demo v2 from `1542a14`: `build\release\QuartzMIDI-demo-v2.zip` (426 MB,
  SHA256 `E8CF6720AB5B164C2E8F094EB354C2B6E94E9E9B2A9C318DEA38D63493D217F7`),
  the staged folder beside it, and `QuartzMIDI-source-1542a14.zip` (2.4 MB).
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Ask the owner what they found in the text of the `1542a14` build and fix the
first thing they name on this branch, then repackage with `make-release.ps1`
and `make-source.py`.
