# Start here

Entry point for the next session. Updated 2026-09-14 at `input-path-r5`
(`d54d8b3`). Then read `HANDOFF.md` for architecture and decisions,
`SHELL-GAPS.md` for what the shell still owes, and `SEATS.md` for who does what.

## Goal

One Windows app that does everything the original MIDI++ window did and more.
The ImGui shell (`ui/`, `build\shell\MIDIShell.exe`) replaces the Win32 window
(`MIDI++/`, `x64\Release\MIDI++.exe`); both share `PlaybackCore` through
`ShellEngine`. Nothing the original had is optional (`SHELL-GAPS.md`).

## Seats

- The panel seat, a second assistant in another app, owns `ui/`, including
  `ShellEngine::Action` and `EngineSnapshot`. Claude never extends those two
  types. It asks for them and waits.
- Claude owns `MIDI++/`, `tests/`, specs, and engine-side seams in `ui/`.
- Cursor-driving or DPI-sensitive checks go to the panel seat (`SEATS.md`).
  Hand it work as a `PROMPT-*.md` file the owner pastes in.

## Decisions made, do not reopen

- Velocity stays on a modifier (`VELOCITY_MODIFIER`: alt, ctrl or shift). A bare
  velocity key would play a note. The tap is four events in one `SendInput`
  call.
- Injection never shares a thread with the message loop (`HANDOFF.md` section 4).
- Legit mode applies at dispatch, never at parse time (`LEGIT-MODE.md`).
- Timing numbers stop at the keyboard hook. Never call them end-to-end latency.
- A skin chooses colour only. Every skin is 1090 x 635 collapsed.
- Built-in velocity curves are six. Improved Low Volume, Logarithmic and
  Exponential were stretched to reach step 31 (`VELOCITY-CURVES.md`, option 2).
- The sixth built-in, **S-Curve**, is the owner's R5 curve "radiant grand",
  unstretched, so it tops out at step 29. It was named Pro until 2026-09-11.
- Linear Coarse and Fine stay as the R5 shipped them (one curve, offset by 2).
- Drum detection only labels tracks, as the original does. Auto-transpose goes
  through the shell's Transpose and never types arrow keys.
- UI copy follows `HANDOFF.md` section 15, and attribution follows section 13.

## Verified facts

- `D:\MIDI++ 1.0.4.R5 Release\` is the original app. Its exe's five curve tables
  are byte-identical to ours, and its `config.json` holds eight custom curves.
- `SHELL_VELOCITY` stores a `builtins` count, so a config saved with five
  built-ins reopens on the same custom curve.
- `drum_flags` never removed notes, upstream or here.
- midi-converter (ArijanJ) and LioK251/mp3converter are both MIT.
  shizuhaki/miditoqwerty has no licence, so nothing may be copied from it.
- The display runs at 125%. Read `tests/NativeShell.ps1` before scripting any
  click or screenshot.

## Work completed, 2026-09-11

- S-Curve as a built-in, the three stretched curves, and the preset index
  migration. `BuiltinCurveTests`.
- Drum detection and auto-transpose honoured from the config.
  `DrumDetectionTests`.
- `sheet::Style` and `sheet::ToHtml` in `MIDI++/SheetExport.hpp`: the
  midi-converter notation ported, with its licence at
  `third_party/midi-converter/LICENSE`, copied beside the shell exe.
  `SheetStyleTests`.
- Five new mutations in `tests/run-shell-parity-mutations.ps1`.
- `PROMPT-S-CURVE-AND-SWITCHES.md` written for the panel seat.

## Unresolved

- **Panel seat, not started as of 2026-09-14:** `PROMPT-S-CURVE-AND-SWITCHES.md`
  has three pieces:
  1. Rename Pro to S-Curve in `skin-system.html` and `docs/design/`.
  2. Settings switches for drum detection and auto-transpose.
  3. The export menu that replaces Copy as sheet, with a control for every
     `sheet::StyleOptions` field, image export, and per-region transpose.
  Until it lands, users can reach none of it. The owner's UI pass items in
  `SHELL-GAPS.md` are also still owed.
- **MP3 to MIDI:** blocked on the owner's install decision. LioK's converter
  needs Python 3.10+, PyTorch, Transkun, yt-dlp and FFmpeg; none is installed.
  The options were: install for testing (list each package, source and size for
  approval first), bundle a helper with a release, or not now. Unanswered.
- **Needs the owner at the keyboard:** delivery into a game from the shell,
  Wooting feel and held-Shift black keys, two devices played at once, live
  curve reconnection, and a mixed-DPI monitor move.

## Validation actually run, at `d54d8b3`

- `tests\run-shell-tests.ps1`: all passed.
- `tests\run-shell-parity-mutations.ps1`: 19 of 19 killed, sources restored.
- `ui\MIDIShell.vcxproj` and `MIDI++.sln` Release x64 both built.
- `run-shell-tests.ps1 -Render` last passed at `a92ea8e`. It was not rerun after
  the sheet port, which changed no UI.
- Not run: `run-native-tests.ps1` and `run-latency-tests.ps1`. Both take the
  cursor and type into whatever has focus, so they need the owner's OK.
  Nothing was played into a game.

## Build and test

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'ui\MIDIShell.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& .\tests\run-shell-tests.ps1 -Render
& .\tests\run-shell-parity-mutations.ps1
```

Shell tests capture injection in process and are always safe. In PowerShell
5.1, do not call them with `*>&1`, because native stderr aborts the script.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus; `upstream` is Zephkek/MIDIPlusPlus.
  `main` stays at `e37ba7e`. Pushing without asking is authorized.
- `input-path-r5` = `claude/curves-pro-drums` = `d54d8b3`, pushed. It contains
  `output-panel`, `claude/velocity-graph-parametric` and `velocity-editor`.
- Main checkout is on `output-panel` with a modified `x64/Release/MIDI++.exe`;
  `D:\Dev\mpp-panels` is on the old `astra/shell-parity` (`bdd7e85`).
- Never commit `x64/Release/midi/` (the owner's music) or `build/`.

## Next action

Ask the owner the MP3 install question above, with its three options, and act
on the answer. Every other engine item is done or waiting on the panel seat.
