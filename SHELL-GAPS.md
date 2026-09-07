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

## Built in the engine, unreachable from the shell

### AutoVol

Original: `Advanced` card, toggles `enable_volume_adjustment` through
`toggle_volume_adjustment()`, and on switch-on it focuses the game and runs
`calibrate_volume()` to drive the in-game volume to a known point.
`MIDI++.cpp:2055`. It re-calibrates after every file load, `MIDI++.cpp:1735`.

Engine: live on both paths. `MIDI2Key.cpp:467` for live input,
`AdjustVolumeBasedOnVelocity` for autoplay. Both read
`enable_volume_adjustment`, which `PlaybackSystem.hpp:245` defaults to false.

Shell: nothing anywhere in `ui/` mentions it, so it is false forever.

One thing to design rather than guess at: `calibrate_volume()` takes the
desktop. It focuses the game window and sweeps the volume keys. In the original
that was acceptable because the original window was the thing you alt-tabbed
away from. The shell needs to say what it is about to do and when, because a
checkbox that silently steals focus and types arrows is its own bug report.
Design it, do not drop it.

### 88-Key mode

Original: `Advanced` card, first button, `MIDI++.cpp:1231`.

Shell: `ShellEngine.cpp:159` sets `eightyEightKeyModeActive = true` for every
player it constructs and nothing sets it back. Anyone playing a game with the
61-key layout gets the 88-key map and no way to say so. This one is audible.

### OutRange

Original: `Advanced` card, `ENABLE_OUT_OF_RANGE_TRANSPOSE`. Folds notes below
the layout up and notes above it down instead of dropping them.

Shell: absent, and doubly unreachable, because the fold only runs when 88-key
mode is off and the shell pins that on. `precomputeAllMappings` takes the
identity branch either way. Both switches have to come back for either to work.

### Legit Mode

Original: `Config` card checkbox, `MIDI++.cpp:1345`.

Shell: `ShellEngine.cpp:348` hardcodes `legit_mode_active = false` on load.
`LEGIT-MODE.md` records that it sounds unconvincing, which is a reason to
default it off and a reason to keep improving it. It is not a reason to remove
the switch: the original offers it, so the shell offers it.

### Drum detection and auto-transpose

`ShellEngine.cpp:345` forces `DETECT_DRUMS` and `auto_transpose.ENABLED` off at
load, because the parser's heuristic removes notes before the track list is
built. That is a real constraint and it is also a bug to fix, not a permanent
override. Until it is fixed the shell has to show these as unavailable and say
why; after it is fixed they are ordinary settings again.

## Absent outright

- **Shuffle Play.** `MIDI++.cpp:1301`, with the end-of-song handler at 2194
  picking the next file at random.
- **Opacity slider.** `MIDI++.cpp:1314`, window alpha.
- **Prev / Next.** `MIDI++.cpp:1330`, step through the file list.
- **MidiConnect.** `Panels.cpp:217` draws a disabled pill reading "Unavailable
  in this shell". The class is built and working, so that pill is a promise to
  finish, not an answer.
- **Log panel and Clear Log.** `MIDI++.cpp:1378`. Without it, everything the
  engine prints is invisible to anyone who did not launch from a console, which
  is every tester who has filed a report so far.

## Also owed, from elsewhere

Listed here so one page holds the whole obligation.

- **MIDI output**, the route switch specified in `MIDI-OUTPUT.md`.
- **The transport bindings on screen.** F1 to F4 are registered at startup and
  named nowhere. `HANDOFF.md` section 15.
- **The countdown before playback starts**, removed in the same change that
  registered those hotkeys, and asked for again by a tester on 2026-09-07.
- **A settable velocity key.** The velocity burst holds ALT and taps a
  character, and ALT is hardcoded at `PlaybackCore.cpp:922` and
  `MIDI2Key.cpp:460`. Alt+1 is Roblox's capture shortcut, which is why velocity
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
4. MidiConnect, OutRange, legit mode, shuffle, Prev/Next, opacity.
5. MIDI output, which is new rather than owed, and the largest.
6. Drum detection and auto-transpose, once the parser heuristic is fixed.
