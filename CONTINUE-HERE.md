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
  explanatory text means the control was not clear, so fix the control and
  cut the text. Keep a sentence only when it says what the control cannot.
- The caption follows the skin through DWM (`ApplyCaption` in `ui/Shell.cpp`).
- Key Mapping never opens on its own; `keyMappingOpen` is not saved.
- Solo Piano is one toggle (`SoloPianoApplied`); a second click is Unmute All.
  A part named Flute, Strings, Drums and so on with no program change on its
  channel is not piano (`DescribeTracks`); an explicit program always wins.
- Skins are Blue, Blue Dark, Orange and Orange Dark. A skin chooses colour
  only; 1090 x 635 collapsed, floor 900 x 610.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- `ui/Panels.cpp`: the Export menu and its confirmation (about 2100 to 2190),
  the settings descriptions (246 to 259, 720, 1367 to 1411, 1523 to 1585),
  the toggle tooltips (1814, 2297), the AutoVol tooltip (731).
- `MIDI++/SheetPage.hpp`: the editor page's eight `<p class="note">` lines
  (923 to 962). `tools/release-README.txt`: the tester notes.
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

## Unresolved

- **Copy pass, owner-asked:** the tooltips, settings descriptions, the
  confirmation and the editor page notes read as an assistant wrote them.
  Go through every string in the files above and cut what a clear label
  already says. The owner's examples: "Files already there are overwritten.
  Progress shows in the status bar; Stop is in the Export menu." and "A note
  this close to the one before joins its chord."
- **Owner to test on the `9fa4605` build:** the Export menu and the
  confirmation; the Solo Piano toggle in the mini window; sections on a real
  file; Save image from a page opened off disk.
- **Needs the owner at the keyboard:** game delivery, Wooting, two devices,
  MIDI out, live curve reconnection, mixed DPI, the 900 x 610 clamp.

## Validation actually run

- `ShellTests.exe` all PASS, parity passed, `RenderTests.exe` all PASS at
  100 to 200% in four skins, including `library-save`, at `9fa4605`.
- The source zip's scan found nothing at `9fa4605`; `make-source.py` ran
  from the repo and reproduced it.

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
- Demo v2 from `9fa4605`: `build\release\QuartzMIDI-demo-v2.zip` (426 MB,
  SHA256 `0AFAF5425F0FCD24847431C3A52A4353E3F4118869FA224EFE4BA5406F89C787`),
  the staged folder beside it, and `QuartzMIDI-source-9fa4605.zip` (2.4 MB).
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Do the copy pass: every string in `ui/Panels.cpp`, `MIDI++/SheetPage.hpp`
and `tools/release-README.txt` named above, one decision per string, then
run the tests, repackage with `make-release.ps1` and `make-source.py`.
