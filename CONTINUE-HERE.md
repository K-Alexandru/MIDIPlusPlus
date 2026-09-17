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

## Decisions made, do not reopen

- The product name is QuartzMIDI: the window title, the exe, the icon, the
  package folder and zip, the sheets folders. MIDI++ survives only in source
  tree names (`MIDI++/`, `MIDIShell.vcxproj`, the window class) and credits.
- The caption follows the skin through DWM (`ApplyCaption` in `ui/Shell.cpp`):
  strip colour, primary ink, dark flag. Windows 10 gets dark or light only.
- Key Mapping never opens on its own; `keyMappingOpen` is not saved.
- Solo Piano is one toggle (`SoloPianoApplied`); a second click is Unmute All.
- Velocity stays on a modifier; the tap is four events in one `SendInput`.
  Injection never shares a thread with the message loop (`HANDOFF.md`
  section 4). Legit mode applies at dispatch (`LEGIT-MODE.md`).
- Skins are Blue, Blue Dark, Orange and Orange Dark (Orange was Terracotta).
  A skin chooses colour only; 1090 x 635 collapsed, floor 900 x 610.
- Sheet settings live in the editor page alone. The page is
  `%TEMP%\QuartzMIDI sheets\<stem>.html`; sheet files go under the sheets
  folder (`SHELL_SHEETS_FOLDER`, default a "<MIDI folder> sheets" sibling).
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.
- Anything sent out carries no account name, assistant name or working doc:
  the binary's converter user agent is plain, and `tools/make-source.py`
  zips only `ui/`, `MIDI++/`, `third_party/`, config and LICENSE, scrubbed
  and scanned. The library save asks before it writes.

## Relevant files

- `ui/Shell.cpp`: `ApplyCaption`, the window class with the icon, `wWinMain`.
- `ui/Panels.cpp`: `DrawTransportHints` (keycaps), the Solo Piano toggle,
  `TransportBody` (`active`), `LoadPreferences`. `ui/TrackModel.hpp`: the row
  predicates. `ui/ShellEngine.cpp`: `SoloPiano`/`UnmuteAll` cases.
- `ui/Shell.rc`, `ui/QuartzMIDI.ico`, `tools/gen-icon.py`, `tools/make-release.ps1`,
  `tools/make-source.py`, `tools/release-README.txt`, `ui/TrackModel.cpp`.

## Verified facts

- A running `QuartzMIDI.exe` blocks the shell link step (LNK1168) and, run
  from `build\release\QuartzMIDI`, blocks `make-release.ps1` too.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a
  click. `Start-NativeShell` waits for the title `QuartzMIDI`.
- PowerShell 5.1: never redirect a native exe with `2>&1` or `*>` under
  `$ErrorActionPreference = 'Stop'`; run `run-shell-tests.ps1` bare.
- Actions before `CurveSelect` in the enum are dropped unless the generation
  matches; a new engine's first snapshot is blank, so tests `Await` the config.
- A DAW export puts every part on channel 1 with no program change, so by
  program alone it is all piano; `DescribeTracks` lets the name decide then.

## Work completed, 2026-09-16 (owner-asked `ui/` exceptions)

- Rename to QuartzMIDI across the title, exe, package, sheets folders, tests,
  release script and tester README. `TargetName` in `MIDIShell.vcxproj`.
- Caption colours from the skin; an icon (blue square, white prism) as
  resource 1, set on the window class.
- Key Mapping starts closed every run. Terracotta skins renamed Orange.
- Hotkey legend as keycaps in the Playback header and mini mode.
- Export menu items say what they do, in the label and on hover; the
  library save opens a confirmation (count, files, folder) first.
- Solo Piano: one toggle with icon, tooltips and the accent outline when on,
  disabled when every track is piano; a part named Flute, Strings, Drums and
  so on with no program on its channel counts as non-piano.

## Unresolved

- **Owner to test on the `9fa4605` build:** the Export menu and the library
  save's confirmation; the Solo Piano toggle in the mini window; sections on
  a real file; Save image from a page opened off disk.
- **Needs the owner at the keyboard:** game delivery, Wooting, two devices,
  MIDI out, live curve reconnection, mixed DPI, the 900 x 610 clamp.

## Validation actually run

- `ShellTests.exe` all PASS, parity passed, `RenderTests.exe` all PASS at
  100 to 200% in four skins, including the new `library-save` scenario.
- `NativeShell.ps1` launched three skins: caption, title, icon and keycaps in
  the captures, Key Mapping shut. The owner saw Solo Piano work on their file.

## Build and test

```powershell
& .\tests\run-shell-tests.ps1 -Render
& .\tests\run-shell-parity-mutations.ps1
& .\tools\make-release.ps1
python .\tools\make-source.py
```

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus; `upstream` is Zephkek/MIDIPlusPlus.
  `main` stays at `e37ba7e`. Pushing without asking is authorized.
- The main checkout `D:\Dev\MIDIPlusPlus-modded` is on this branch, pushed,
  clean apart from the owner's `x64\Release\MIDI++.exe` and `x64\Release\midi\`;
  `D:\Dev\mpp-panels` is the panel seat's, leave it.
- Demo v2 from `9fa4605`: `build\release\QuartzMIDI-demo-v2.zip` (426 MB),
  SHA256 `0AFAF5425F0FCD24847431C3A52A4353E3F4118869FA224EFE4BA5406F89C787`,
  the staged folder `build\release\QuartzMIDI\` the owner runs from, and
  `QuartzMIDI-source-9fa4605.zip` (2.4 MB) from `make-source.py`.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Ask the owner what they found on the `9fa4605` demo v2 build and fix the first
thing they name on this branch, then repackage with `.\tools\make-release.ps1`.
