# What the shell is missing from the original window

Written 2026-09-07. This is an inventory, not a plan: every control the
original `MIDI++.cpp` window has, checked against what the ImGui shell can
reach. Panel work, so it belongs to whoever owns `ui/`.

The pattern worth noticing before the list: most of these are not unwritten
features. The engine still has them, working, and the shell simply never sets
the flag, so they are permanently off and no user can tell they exist. That is
the same defect as the undiscoverable transport in `HANDOFF.md` section 15,
one step worse.

## Missing, engine side already works

### AutoVol

Original: `Advanced` card, toggles `enable_volume_adjustment` through
`toggle_volume_adjustment()`, and on switch-on it focuses the game and runs
`calibrate_volume()` to drive the in-game volume to a known point.
`MIDI++.cpp:2055`. It re-calibrates after every file load, `MIDI++.cpp:1735`.

Engine: live on both paths. `MIDI2Key.cpp:467` for live input,
`AdjustVolumeBasedOnVelocity` for autoplay. Both read
`enable_volume_adjustment`, which `PlaybackSystem.hpp:245` defaults to false.

Shell: nothing anywhere in `ui/` mentions it, so it is false forever. The
feature is complete and unreachable.

The part that needs a decision rather than a checkbox: `calibrate_volume()`
takes the desktop. It focuses the game window and sweeps the volume keys. In
the original that was acceptable because the original window was the thing you
alt-tabbed away from. Decide what it does in the shell before wiring the
toggle, because a checkbox that silently steals focus and types arrows is worse
than no checkbox.

### 88-Key mode

Original: `Advanced` card, first button, `MIDI++.cpp:1231`.

Shell: `ShellEngine.cpp:159` sets `eightyEightKeyModeActive = true` for every
player it constructs and nothing ever sets it back. Anyone playing a game with
the 61-key layout gets the 88-key map and no way to say so.

### OutRange

Original: `Advanced` card, `ENABLE_OUT_OF_RANGE_TRANSPOSE`. Folds notes below
the layout up and notes above it down, instead of dropping them.

Shell: absent, and doubly unreachable, because the fold only runs when 88-key
mode is off and the shell pins that on. `precomputeAllMappings` takes the
identity branch either way.

### Legit Mode

Original: `Config` card checkbox, `MIDI++.cpp:1345`.

Shell: `ShellEngine.cpp:348` hardcodes `legit_mode_active = false` on load.
`LEGIT-MODE.md` records the verdict that it sounds wrong, so off is defensible,
but the original still offers it and the shell does not say it decided.

### Drum detection and auto-transpose

`ShellEngine.cpp:345` forces `DETECT_DRUMS` and `auto_transpose.ENABLED` off at
load, for a stated reason: the parser's heuristic removes notes before the
track list is built. Both are config options a user can set and the shell
overrides silently. If the override is right, the config fields should say so
where the user reads them, rather than being quietly ignored.

## Missing outright

- **Shuffle Play.** `MIDI++.cpp:1301` and the end-of-song handler at 2194 pick
  the next file at random. Nothing in `ui/`.
- **Opacity slider.** `MIDI++.cpp:1314`, window alpha. Nothing in `ui/`.
- **Prev / Next.** `MIDI++.cpp:1330`, step through the file list. Nothing in
  `ui/`.
- **MidiConnect.** `Panels.cpp:217` draws it as a disabled pill reading
  "Unavailable in this shell." That is at least honest, unlike the rest of this
  list, but the class is built and working.
- **Log panel and Clear Log.** The original has a log card,
  `MIDI++.cpp:1378`. The shell has no log, so nothing the engine prints to
  stdout is visible to a user who did not launch it from a console.

## Present and equal or better

Load, Play/Pause, Restart, Skip, Rewind, Speed, Velocity, Sustain, Sustain
Cutoff, velocity curve selection and editing, Transpose, Refresh MIDI, Tracks
with mute and solo, Midi2Key live input, and latency measurement, which the
shell has and the original never had beyond one button.

## Suggested order

Two of these are user-visible regressions rather than absences, so they go
first: 88-Key mode and AutoVol are features people had and lost. Shuffle,
Prev/Next and Opacity are conveniences. The log is worth having before the next
tester report, because right now a tester can only describe what they heard.
