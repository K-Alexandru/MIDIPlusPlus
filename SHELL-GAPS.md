# What the shell still owes the original window

Written 2026-09-07, revised the same day. This is a completeness checklist, not
a triage list. The goal is one app that does everything, so nothing on this
page is optional and nothing here gets closed by deciding it does not matter.
Sequencing is the only judgement call, and sequencing is not scope.

Every control in `MIDI++.cpp` was checked against the shell one at a time. The
pattern worth noticing: most of these are not unwritten features. The engine
still has them, working, and the shell never sets the flag, so they are
permanently off and no user can tell they exist. That is the defect in
`HANDOFF.md` section 15 one step worse: not merely undiscoverable, unreachable.

## Done

- **88-Key mode**, `0248ff0`. Selectable and persistent, with the outgoing
  layout's held keys released before the incoming one can type. The line that
  pinned it now reads the selection, `ShellEngine.cpp:212`.
- **AutoVol**, `dea0965`. Window choice, a warning, a cancellable countdown,
  confirmed focus, and one calibration sweep instead of the original's two.

Both are guarded by `ShellTests.cpp` and mutation-tested in `d7a8a79`: the
AutoVol sweep count fails if the duplicate call returns, and the layout case
fails if the pin does. The layout test did not catch the pin until it was
rewritten to play from a restarted session, which is the path the pin lived on.

Corrections these produced, from the panel seat reading the code rather than
this page: `calibrate_volume()` does not focus anything, `FocusRobloxWindow()`
at `MIDI++.cpp:2060` does; and it runs twice on every enable, once inside
`toggle_volume_adjustment()` at `PlaybackCore.cpp:1180` and once from the
caller at `MIDI++.cpp:2061`. **The duplicate is still in the original window's
handler and is the engine seat's to remove.**

## Built in the engine, unreachable from the shell

### OutRange

Original: `Advanced` card, `ENABLE_OUT_OF_RANGE_TRANSPOSE`. Folds notes below
the layout up and notes above it down instead of dropping them.

Shell: absent, and doubly unreachable, because the fold only runs when 88-key
mode is off and the shell pins that on. `precomputeAllMappings` takes the
identity branch either way. Both switches have to come back for either to work.

### Legit Mode

Original: `Config` card checkbox, `MIDI++.cpp:1345`.

Shell: `ShellEngine.cpp:449` hardcodes `legit_mode_active = false` on load.
`LEGIT-MODE.md` records that it sounds unconvincing, which is a reason to
default it off and a reason to keep improving it. It is not a reason to remove
the switch: the original offers it, so the shell offers it.

### Drum detection and auto-transpose

`ShellEngine.cpp:447` forces `DETECT_DRUMS` and `auto_transpose.ENABLED` off at
load, because the parser's heuristic removes notes before the track list is
built. That is a real constraint and it is also a bug to fix, not a permanent
override. Until it is fixed the shell has to show these as unavailable and say
why; after it is fixed they are ordinary settings again.

## Absent outright

- **Shuffle Play.** `MIDI++.cpp:1301`, with the end-of-song handler at 2194
  picking the next file at random.
- **Opacity slider.** `MIDI++.cpp:1314`, window alpha.
- **Prev / Next.** `MIDI++.cpp:1330`, step through the file list.
- **MidiConnect.** `Panels.cpp:222` draws a disabled pill reading "Unavailable
  in this shell". The class is built and working, so that pill is a promise to
  finish, not an answer.
- **Log panel and Clear Log.** `MIDI++.cpp:1378`. Without it, everything the
  engine prints is invisible to anyone who did not launch from a console, which
  is every tester who has filed a report so far.

## Reported by testers, not yet addressed

Added 2026-09-07 after checking the Discord threads against what actually
shipped. Neither is a crash, and both are the app's fault under section 15.

- **The MIDI device list is unreadable.** `EnumerateMidiInputs` lists WinRT
  ports bare, WinMM ports with `(WinMM)` appended only where the name already
  appeared, and every Kernel Streaming pin with `(KS)`. One physical piano
  therefore appears three times, and software ports like VirtualMIDISynth sit
  at the top of the list looking exactly like hardware. A tester on 2026-09-06
  asked what KS was, reported the list "says virtualmidisynth", and found their
  piano only by hunting. Kernel Streaming itself works: the suite enumerates
  its pins and routes its ids. What is broken is being asked to know what a
  transport is in order to choose a keyboard. Group the rows by device and let
  the transport be a property of the chosen row, not three rows.
- **Nothing warns that the app types into whatever has focus.** The 88-key
  layout binds the lowest notes to `ctrl+` combinations, so `G#1` is `ctrl+w`.
  A tester played it with a browser focused and lost the tab, then could not
  screenshot the bug because playing again closed the window again. The mapping
  is correct and the behaviour is inherent to typing keystrokes at another
  program. Saying so before the first note is not.

## Owner's UI pass, 2026-09-07

Found by looking at the built shell. Panel work, with anchors so nobody has to
find them twice.

- **SOLO is not centred.** `Panels.cpp:1586`. MUTE and SOLO are centred by the
  same arithmetic, so if only one looks wrong the header rectangle is wrong,
  not the centring.
- **Files can only be sorted by name.** `Panels.cpp:1319` is one button
  toggling `descendingNames_`. `MidiEntry` already carries `bytes`, so size
  needs no engine change; a date sort needs a field on `MidiEntry`, which is
  the panel seat's own type.
- **The highlight above buttons stops at the corners.** It runs the straight
  span only and does not follow the rounded ends, so it reads as a line rather
  than a highlight. `SkinDraw.cpp`.
- **The top-right icons move between mini and regular mode.** Settings goes
  from hard right to the middle while zoom takes the right-hand slot. The
  positions should not depend on the mode.
- **Pills change size, weight and position between modes.** "No MIDI input"
  appears to go from bold to regular and shifts, and MIDI2Key shifts too, in
  both cases with room to spare either way. Pick one treatment and use it in
  both modes; if something has to shrink to fit mini mode, shrink it in
  regular mode as well so the two agree.
- **Refresh is the only utility control spelled out in words.**
  `Panels.cpp:1327`. `Icon::Refresh` exists and is used at 582, 932, 1232 and
  1449. Section 15 already requires icons here.
- **"Copy as sheet" is the primary button and should not be.**
  `Panels.cpp:1404`. It is one export among several, so it belongs in a menu
  whose other entries are the other export formats.
- **"Loading..." overlaps the separator.** `Panels.cpp:1103`. It flashes for a
  frame when a file is clicked and the text crosses the hairline while it does.

## Conversion pipeline, asked for 2026-09-07

Not parity and not panel-only, so it is scoped before it is built.

- **MP3 to MIDI**, the converter a community member wrote, asked for by a
  tester as well.
- **MIDI to coloured sheets**, from the author of MIDIToQWERTY, which has
  options this fork's `SheetExport.hpp` does not.

Both are third-party code. This fork is GPLv3, so each one needs its licence
read before a line of it is copied, and attribution under `HANDOFF.md` section
13, which is not optional. Neither is a panel task: the panel owes the menu
that `Copy as sheet` becomes, and the engine owes what the menu entries do.

## Also owed, from elsewhere

Listed here so one page holds the whole obligation.

- **MIDI output**, the route switch specified in `MIDI-OUTPUT.md`.
- **The transport bindings on screen.** F1 to F4 are registered at startup and
  named nowhere. `HANDOFF.md` section 15.
- **The countdown before playback starts**, removed in the same change that
  registered those hotkeys, and asked for again by a tester on 2026-09-07.
- **A settable velocity key.** The velocity burst holds ALT and taps a
  character, and ALT is hardcoded at `PlaybackCore.cpp:922` and
  `MIDI2Key.cpp:459`. Alt+1 is Roblox's capture shortcut, which is why velocity
  is off by default since `954b3cd`. Whether a given game can be told to listen
  for a different key is the game's business; offering the choice is ours.

## Already present

Load, Play/Pause, Restart, Skip, Rewind, Speed, Velocity, Sustain, Sustain
Cutoff, velocity curve selection and editing, Transpose, Refresh MIDI, Tracks
with mute and solo, Midi2Key live input, and latency measurement, which the
original never had beyond a single button.

## Sequencing

Order of work only. Nothing below the line gets dropped for being below it.

1. 88-Key mode and AutoVol, because those are features people had and lost.
2. The log panel, because the next tester report is written blind without it.
3. Transport bindings and the countdown, which are one piece of work.
4. The device list, which is the one a tester has already tripped over.
5. MidiConnect, OutRange, legit mode, shuffle, Prev/Next, opacity, and the
   warning before the first keystroke.
6. MIDI output, which is new rather than owed, and the largest.
7. Drum detection and auto-transpose, once the parser heuristic is fixed.
