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

OutRange and Legit Mode were both here and are both reachable now. OutRange is
a Settings switch that pauses autoplay and releases held keys before remapping,
`Panels.cpp:1087`, killed by two mutations in the `out-range` group. Legit Mode
is a Settings switch that survives Load, `ShellEngine.cpp:253`, killed by the
`legit-disabled-on-load` mutation. What remains here is the one with a real
constraint under it.

### Drum detection and auto-transpose

**Done 2026-09-11, and the premise here was wrong.** The heuristic never
removed a note, in this fork or upstream: `process_tracks` fills `drum_flags`
and nothing reads it but the original window's track list, which appends
"(Drums)". The shell now honours `DETECT_DRUMS` the same way. A detected track
is labelled "(Drums)", stops counting as piano, and Solo Piano mutes it. That
matters for a kit that is not on channel 10, which `DescribeTracks` alone reads
as piano.

Auto-transpose honours `AUTO_TRANSPOSE.ENABLED` too, with one deliberate
difference. The original types the game's arrow keys at play start into
whatever has focus; the shell applies the suggestion through its own Transpose
at load, where the user can see and change it, and never sends an arrow.

Both are config-only. Settings switches for them are an `EngineSnapshot` field
and an `Action` each, which belong to the panel seat.

## Absent outright

All five shipped in `c254d0a` and `bdd7e85`, and this page said otherwise for
five commits because nobody came back to it. Shuffle Play, the opacity slider,
Prev / Next, MidiConnect and the log panel with Clear Log are all in the shell,
covered by the `library`, `connect` and `log` test groups and gated by four
mutations in `run-shell-parity-mutations.ps1`.

Left over from that pass: **OutRange is reachable but still needs 61 Keys
selected**, which is the original's constraint rather than a shell one, and the
Settings switch says so in its disabled tooltip.

## Reported by testers, addressed since

Added 2026-09-07 after checking the Discord threads against what actually
shipped. Neither was a crash, and both were the app's fault under section 15.
Both are answered, and the second was answered by deleting rather than adding.

- **The MIDI device list is unreadable.** Fixed in `bdd7e85`: rows are grouped
  by device and the transport is a property of the chosen row, with a fallback
  to individual ids when two devices genuinely share a name, gated by the
  `same-name-device-merge` mutation. The original report is kept below because
  it is the only written record of what the list looked like.
- **Nothing warns that the app types into whatever has focus.** Answered in
  `c9ea480`, and not the way this page proposed. A "Before you play" modal and
  a caption under the pills both said the same thing before the user had done
  anything, so both went, along with the acknowledgment gate on output. The
  warning gate machinery is still in `ShellEngine.cpp` and still tested, so
  reinstating a warning is a UI decision rather than a rebuild. **If the owner
  wants a warning back, this is the open question: what would it say that the
  first `ctrl+w` does not, and when would it be worth interrupting for.**

The reports as filed:

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
  - **Undo and redo, with A/B demoted.** A sweep replaces a whole region in one
    gesture, so there has to be a way back. HANDOFF section 12 point 4 was
    revised on 2026-09-07: undo and redo become the primary control, and A/B
    moves to Settings as a hidden option rather than being deleted. It works,
    and keeping it a release longer is how we learn whether its pinned
    reference is missed. It just stops holding a button in the editor.
- **A bar can barely move.** `Panels.cpp:842` clamps each one between its two
  neighbours' current heights, so on a near-linear curve the travel is about
  one step in 31. Making an audible change means dragging all 32 in order, each
  unlocking a sliver for the next. The clamp itself is right, a curve that goes
  backwards is nonsense, but enforcing monotonicity by pinning the handles is
  what makes the handles useless. Smoothing enforces it for free.

The part section 12 called the key feature, the histogram of what the player
actually played, is built and wired to `velocity_telemetry`. The editor has the
hard half and lost the easy half.

## The graph redraws the curve instead of drawing it: done

Fixed as specified below. `VelocitySamples` is gone and `VelocityCurveAt` reads
the table parametrically. Measured over all five built-ins: the drawn curve now
passes through every reachable table point exactly, where the resampling missed
16 to 29 points per preset by as much as 0.95 of a step. It is monotone and
inside the graph over 1271 samples of each preset, and it ends where the table
saturates, which for Logarithmic is 17/31 and was already true of both readings.

`VelocityCurveDrawingTests` in `ShellTests.cpp` holds all of that, and the
`curve-resampled-on-uniform-grid` mutation kills any return to a uniform
resample. A repeated threshold names a bucket `VelocityBucket` can never
return, so those are skipped rather than drawn as a vertical rise at the right
edge, and the test makes no promise about them.

Still open from this section: the engine-side tuning question in its last
paragraph, which is the owner's call and not a drawing problem.

The report as filed:

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

## Pro, now S-Curve: done 2026-09-11

The numbers were on this machine all along, in `D:\MIDI++ 1.0.4.R5 Release\config.json`
as the custom curve "radiant grand". It is now the sixth built-in in
`PlaybackCore.cpp` with exactly those 32 values, so it cannot be deleted. The
owner renamed it from Pro to S-Curve the same day, because that describes its
shape. Custom curves are numbered after it, and `SHELL_VELOCITY` records a
`builtins` count so a file saved with five built-ins reopens on the same
custom curve rather than on S-Curve. It tops out at step 29 of 31 because its
last three values are 127 as tuned; it was not stretched.

The rest of this section is the report as filed.

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

### Licences, read 2026-09-11

- **The coloured sheets are ArijanJ's `midi-converter`**,
  github.com/ArijanJ/midi-converter, MIT, "Copyright (c) 2024 ArijanJ". MIT
  code can be carried into a GPLv3 project as long as that notice goes with
  it. It is a Svelte web app; the sheet logic is in `src/utils`, mainly
  `VP.js`, `MIDI.js`, `SheetCombine.js`, `Rendering.js` and `Settings.js`.
  Porting it means translating JavaScript into C++, not linking it.
- **What it has that `SheetExport.hpp` does not**, from `Settings.js` and
  `Rendering.js`:
  - Colour by rhythm: each chord is coloured by the time to the next one, green
    for long notes through red for short ones.
  - Quantize window, default 35, where ours is a fixed 45 ms. Curly braces mark
    a chord that was quantized rather than struck together.
  - Out-of-range notes: shown, underlined and bold, optionally marked with a
    separator. Ours drops them and counts them.
  - Tempo and BPM-change marks, beats per bar, a missing-tempo fallback BPM,
    and a line-break style.
  - Classic chord order, sticky auto-transposition, per-track selection, and
    image export with font and line height.
- **The miditoqwerty projects are not the sheet tool.** ArijanJ/miditoqwerty is
  MIT and shizuhaki/miditoqwerty has no licence file, which means all rights
  reserved: nothing from the second may be copied. Neither makes sheets.
- **MP3 to MIDI is LioK251's `mp3converter`**, github.com/LioK251/mp3converter,
  MIT, "Copyright (c) 2025-2026 LioK", named by the owner 2026-09-11. It is a
  Python Flask app around the Transkun transcription model, with PyTorch,
  yt-dlp and FFmpeg. A neural model cannot be translated into C++ the way the
  sheet notation was, so using it means running it beside the app as a
  sidecar, which needs Python and those tools installed. None is on this
  machine, so it starts with an install decision for the owner.

### The sheet port, engine side: done 2026-09-11

`sheet::Style` in `MIDI++/SheetExport.hpp` is midi-converter's notation,
translated, with every setting above except the image export and the
interactive per-region transposition, which are UI. `sheet::ToHtml` writes the
coloured sheet as one self-contained page. `SheetStyleTests` covers each
behaviour, and the `quantize-not-chained` and `out-of-range-dropped` mutations
guard the two that matter most. The notice is in
`third_party/midi-converter/LICENSE`.

Nothing reaches a user yet. The menu `Copy as sheet` becomes, its entries and
their settings are an `Action` and `EngineSnapshot` fields, which belong to the
panel seat; `PROMPT-S-CURVE-AND-SWITCHES.md` piece three has them.

## Also owed, from elsewhere

Listed here so one page holds the whole obligation.

- **MIDI output**, the route switch specified in `MIDI-OUTPUT.md`.
- **The transport bindings on screen.** F1 to F4 are registered at startup and
  named nowhere. `HANDOFF.md` section 15.
- **The countdown before playback starts**, removed in the same change that
  registered those hotkeys, and asked for again by a tester on 2026-09-07.
- **A settable velocity key.** Engine side done 2026-09-09, panel side owed;
  see item 5 under Sequencing. Building it turned up a hole the hardcoding had
  been hiding: `release_keys` lifted Alt and Ctrl unconditionally and nothing
  else, which was exactly right while the tap always held ALT and is a gap the
  size of the third option once it does not. An interrupted tap would have left
  shift down and every later note would have typed its shifted character.

## Already present

Load, Play/Pause, Restart, Skip, Rewind, Speed, Velocity, Sustain, Sustain
Cutoff, velocity curve selection and editing, Transpose, Refresh MIDI, Tracks
with mute and solo, Midi2Key live input, and latency measurement, which the
original never had beyond a single button.

## Sequencing

Order of work only. Nothing below the line gets dropped for being below it.

Items 1 to 5 of the original order are done: 88-Key mode, AutoVol, the log
panel, the transport bindings and the countdown, the device list, MidiConnect,
OutRange, legit mode, shuffle, Prev/Next and opacity. The warning that shared
item 5 was answered by removing it, and whether one comes back is a question
for the owner rather than a task. What is left, reordered 2026-09-09:

1. **Done 2026-09-09.** The velocity editor: anchors, monotone PCHIP, free
   draw, one shared representation, ghost bars, histogram snapping, and undo
   and redo as the primary control. All seven items, with a render scenario at
   every skin and DPI.
2. **Done 2026-09-11, option 2.** Improved Low Volume, Logarithmic and
   Exponential keep their R5 shapes stretched across all 32 steps, so every
   built-in but Pro can play loud. The Linear Coarse and Linear Fine naming is
   still open, see `VELOCITY-CURVES.md`.
3. **Done 2026-09-11.** Pro, from the R5 config. See the Pro section above.
4. MIDI output, `MIDI-OUTPUT.md`. The engine half is built as of 2026-09-09;
   what is left is the panel half, which is a picker and a two-way switch and
   is listed in that file ready to be written. Until it exists no user can
   reach any of it.
5. **Done 2026-09-09.** A settable velocity key, as
   `VELOCITY_MODIFIER` in `config.json`: alt, ctrl or shift, defaulting to alt.
   The config refuses anything that is not a modifier, because the velocity
   characters are the characters the piano mappings use and a bare velocity key
   would play a note, which is the closed decision in `CONTINUE-HERE.md`.
   `velocity_modifier_conflicts()` names the combinations that collide with the
   selected layout, since choosing ctrl makes fifteen taps play a note in the
   88-key layout and the app is the only thing that can work that out. **The
   panel still owes the control and the conflict warning**; the engine will not
   surface either on its own.
6. **Done 2026-09-11, engine side.** Drum detection and auto-transpose; the
   panel still owes their two Settings switches.
7. The conversion pipeline, which starts with reading two licences.

Not on this list because they are not shell work: the Wooting and two-device
checks, which need the owner at the keyboard. The duplicate
`calibrate_volume()` in the original window's handler was removed on
2026-09-09; the fix was the order rather than the deletion, because the sweep
that survived a bare deletion would have been the one that runs unfocused.
