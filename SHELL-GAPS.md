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

## The velocity editor diverged from its own spec

`HANDOFF.md` section 12 calls this the most important UI problem in the project
and settles the question at the end of it: *"Editing 32 discrete steps by hand
is exactly the manual-dragging tedium that made the old one unusable. The steps
are an implementation detail and should be visible but not the editing
surface."* Advanced does exactly that, `Panels.cpp:824`.

Two defects, and the second is why it feels worse than it looks.

- **The 32 steps are the editing surface.** Section 12 says edit a smooth
  curve, sample it to 32, and draw the steps as a faint ghost underneath. Note
  that hiding this under Advanced is not the fix: manual editing still has to
  be worth using, it just is not the primary interface.

  What to build instead, agreed with the owner 2026-09-07: anchor points on the
  curve, as Photoshop's Curves works. Click the line to drop an anchor, drag it
  and the curve bends smoothly through it, drag it off the graph to delete.
  Three or four anchors cover nearly every response anyone wants, and one drag
  changes a region of the player's range instead of one bucket in 32.

  Interpolate between anchors with monotone cubic (PCHIP), so the curve cannot
  fold backwards whatever the anchors do. That is what retires the clamp below:
  monotonicity comes from the interpolation rather than from restricting the
  mouse. The 32 bars stay as the ghost readout section 12 asks for.

  One addition beyond section 12: the played-velocity histogram is already
  drawn, so anchors should snap to the edges of the band the player actually
  plays in. Section 12 opens by saying the user cannot tell what to go for;
  a snap target is that answer made concrete.

  **Free-draw is wanted as a second mode**, owner 2026-09-07, not as a
  someday. Hold and sweep across the graph, the line follows the cursor, and it
  smooths on release. Faster than anchors for a big reshape, worse for
  precision, which is why both exist.

  Build them on one representation, not two. Draw produces a curve, anchors
  edit a curve, and a shared model means sweeping a rough shape and then
  pulling an anchor to refine it is one continuous piece of work rather than a
  mode switch that discards the last one. That model is a smooth curve sampled
  to 32, per section 12, never 32 stored values.

  Four things draw has to get right, all the same principle as the clamp above,
  which is to leave the hand alone and legalise the result:

  - **Only what was swept changes.** Draw over the middle third and the ends
    keep their shape, blended at the join rather than stepping.
  - **Smoothing happens on release, not during.** The line follows the cursor
    exactly while drawing, because a line that fights the hand feels broken,
    and a light smoothing pass afterwards stops hand jitter becoming 32 jagged
    buckets.
  - **Monotonicity is repaired, not enforced.** A sweep that dips, or runs
    right to left, must be allowed to happen and then be made non-decreasing on
    release. Blocking the cursor is what makes the current bars miserable.
  - **Undo and redo, replacing A/B compare.** A sweep replaces a whole region
    in one gesture, so there has to be a way back. HANDOFF section 12 point 4
    was revised on 2026-09-07 for this: undo and redo serve the comparison
    people actually make, and A/B costs 25 references across five snapshot
    fields for a pinned reference the preset list already provides.
- **A bar can barely move.** `Panels.cpp:842` clamps each one between its two
  neighbours' current heights, so on a near-linear curve the travel is about
  one step in 31. Making an audible change means dragging all 32 in order, each
  unlocking a sliver for the next. The clamp itself is right, a curve that goes
  backwards is nonsense, but enforcing monotonicity by pinning the handles is
  what makes the handles useless. Smoothing enforces it for free.

The part section 12 called the key feature, the histogram of what the player
actually played, is built and wired to `velocity_telemetry`. The editor has the
hard half and lost the easy half.

## The graph redraws the curve instead of drawing it

`ui/VelocityModel.hpp:33`. The engine's table says which inputs each output
bucket covers. `VelocitySamples` asks the opposite question, what bucket does
input `round(i * 127/31)` fall in, which resamples a table stepping by 4 on a
grid stepping by 4.097. Measured against Linear Fine: 27 of 32 samples land on
their own index, 5 gain a step at the top, and `samples[30]` and `samples[31]`
are both exactly 1.0, so the drawn line goes flat over its final interval.
That flat step is the visible bump.

No new numbers are needed. Bucket `i` sits at input `thresholds[i]`, so plot it
parametrically, x = `thresholds[i]/127` against y = `i/31`, and the drawing is
the table exactly. `VelocityThresholds` in the same file already keeps built-ins
untouched, with the comment "Preserve built-ins exactly". The drawing does not.

Separately, and engine side: Linear Fine is 2, 6, 10 ... 122 and then 127, so
its last interval is 5 where every other is 4, and Linear Coarse ends 124 then
127, a 3. The other three presets reach 127 early and repeat it, 15 times for
Logarithmic, which is the flat right-hand third of the graph. Whether to
change those is the owner's call, because it changes what the app sounds like.

## Pro is missing because nobody has its numbers

Not a bug. `CONTINUE-HERE.md:310` records the decision: only configured custom
presets are shown and no Pro values were invented for configs that do not have
them. Every `config.json` in this repo and on this machine has
`CUSTOM_VELOCITY_CURVES` empty, so Pro has never existed here as data.

What exists is the name decision, `HANDOFF.md:291`, where Pro carries a
"recommended" tag and "Radiant Grand" is retired as the name of its author's
soundfont; and a shape in the mockup, `skin-system.html:1097`, which is
`x*x*(3-2*x)` with sensitivity 18 and contrast 58. The mockup shape is a
stand-in drawn to make the mockup legible, not a tuning.

Needs the real 32 values from someone's config. Failing that, either ship the
mockup shape and say in the UI that it is a starting point rather than the
original tuning, or retire the name. If Pro ships it belongs beside the other
built-ins in `PlaybackCore.cpp`, not as a custom entry a user can delete.

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
