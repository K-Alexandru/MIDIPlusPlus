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

**Switches added 2026-09-15.** Settings carries "Detect drum tracks" and
"Auto-transpose on load", `Action::DetectDrums` and `Action::AutoTranspose`.
Each writes its config key, flushes the file, sets the `Config` singleton
where `process_tracks` reads it, and reloads the open file so the track list
matches at once, resuming if it was playing. Held by `DrumDetectionTests`,
the `settings-switches` render scenario and the `drum-switch-skips-singleton`
and `auto-transpose-switch-unsaved` mutations.

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

**All eight closed, checked 2026-09-15 against the code and the render
captures:** the headers centre on measured text, files sort by name, size and
date, the highlight follows the rounded shoulders, the top strip and the pills
are the same in both modes, Refresh is an icon everywhere, Export is a menu,
and "Loading..." is a status-bar field under a clip rect. The list stays as
the record of what was asked.

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
`PlaybackCore.cpp`, so it cannot be deleted. The owner renamed it from Pro to
S-Curve the same day, because that describes its shape. Custom curves are
numbered after it, and `SHELL_VELOCITY` records a `builtins` count so a file
saved with five built-ins reopens on the same custom curve rather than on
S-Curve.

**Corrected 2026-09-15.** The 32 values were first copied in as the engine
table, which made the shell draw the inverse of the S the owner had tuned:
flat until input 25, a jump, then a vertical tail. The R5 editor shows a
table as output velocity per step, but the engine reads a table as input
thresholds, so R5 itself played the inverse of what its editor drew. The
built-in is now the inverse table, derived in `BuiltinCurveTests` from the
R5 values as a response, so the shell draws the S the owner drew and the
game plays it. Its softest touch is step 6, where the drawn curve starts,
and it reaches step 31.

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
  sidecar, which needs Python and those tools installed.

### MP3 to MIDI: built 2026-09-14

The owner chose to install for testing and to aim at bundling with a release.
`tools/mp3-to-midi/convert.py` is this fork's own sidecar: a file or link in,
yt-dlp for links, Transkun, one status per line out. It copies no code from
mp3converter. `MIDI++/AudioToMidi.hpp` runs it on its own thread in a job
object. In the shell, the plus button beside Choose MIDI folder opens Convert
audio, and a finished `.mid` is saved in the MIDI folder and rescanned.

The owner let Claude add `ConvertAudio`, `ConvertCancel` and `ConvertProgress`
and the `converting`, `conversionFailed` and `conversionStatus` fields this
once, outside the usual seat split. The popup was reworked on 2026-09-15 at
the owner's request: one primary action, the file picker second, sign-in as
a footnote row.

Still open:
- A release bundle: `converter\` beside the exe with an embeddable Python,
  the packages and `ffmpeg\`. CPU PyTorch alone is several hundred MB.
- GPU speed on this machine. The card is AMD, so PyTorch runs on the CPU;
  about 24 s for 93 s of solo piano.
- **Fixed 2026-09-15:** a direct media link, `upload.wikimedia.org` for
  one, goes through yt-dlp's generic extractor, and Wikimedia answers 403
  unless the client names itself. `download` in `convert.py` retries a 403
  once under the program's own user agent; YouTube's extractor never reaches
  that path. Reproduced and verified with the staged bundle.
- YouTube refuses the owner's connection unless signed in. Sign in to YouTube
  in the popup (`signin.py`, 2026-09-14) fixes it; the owner confirmed the
  window works. A release must bundle `pywebview` for it.

### The sheet port, engine side: done 2026-09-11

`sheet::Style` in `MIDI++/SheetExport.hpp` is midi-converter's notation,
translated, with every setting above except the image export and the
interactive per-region transposition, which are UI. `sheet::ToHtml` writes the
coloured sheet as one self-contained page. `SheetStyleTests` covers each
behaviour, and the `quantize-not-chained` and `out-of-range-dropped` mutations
guard the two that matter most. The notice is in
`third_party/midi-converter/LICENSE`.

**The menu shipped 2026-09-15.** Export in Playback is a menu: Copy as
sheet, Copy styled sheet (`Action::CopyStyledSheet`, `sheet::Style` text to
the clipboard), Save coloured sheet (`Action::SaveSheetHtml`, `sheet::ToHtml`
written beside the MIDI file under its name, and the panel says where), and
Sheet style, a popover with a control for every field of
`sheet::StyleOptions`, saved as `SHELL_SHEET_STYLE` through
`Action::SheetStyle`. Tempo and meter come from `tempoChanges`,
`timeSignatures` and `division` through the `FromTicks` helpers. Per-region
transposition is "Transpose a section" in the same popover: a run named by
its start and end in seconds, as the transport shows them, held per file in
`sheetRegions` and dropped at Load. Image export is left to the coloured
page, which prints and screenshots from the browser; the shell renders no
sheet of its own. Credited in About. Held by `SheetMenuTests`, the
`sheet-style` render scenario and the `section-transpose-ignored` and
`sheet-style-unsaved` mutations.

**Reworked 2026-09-16 at the owner's request, twice.** The page wrote
itself beside the MIDI file without asking, and the popover changed settings
nobody could see; then the popover and the page were two places to set the
same thing. Now Export has two entries. Copy sheet is the plain text for a
chat. Open sheet editor (`Action::OpenSheetEditor`) writes
`sheet::ToEditorHtml` (`MIDI++/SheetPage.hpp`) to `%TEMP%\MIDI++
sheets\<stem>.html` and the panel hands it to the default browser. The page
is midi-converter's own shape: every `StyleOptions` field beside the sheet,
redrawn on each change, a selection over the sheet transposed from a
floating bar, Copy sheet, Save page (the page with its settings, wherever
the browser saves), Print, and Reset settings. The settings are the page's
alone: it remembers them in the browser's localStorage, a saved page keeps
its own, and the app stores none (`SHELL_SHEET_STYLE`, `Action::SheetStyle`,
the region actions and the popover are gone). The page draws with a
JavaScript translation of `sheet::Style`, checks its first render against
the app's text embedded in the page, and `tests/sheet-page-parity.js` (run
by `run-shell-tests.ps1` when node is present) holds that translation to the
app's over the fixture `SheetMenuTests` writes. Nothing is written into the
MIDI folder.

## Also owed, from elsewhere

Listed here so one page holds the whole obligation.

- **MIDI output**, the route switch specified in `MIDI-OUTPUT.md`. Built,
  both halves, by `08aa1ab`; unplayed on hardware.
- **The transport bindings on screen.** F1 to F4 are registered at startup and
  named nowhere. `HANDOFF.md` section 15.
- **The countdown before playback starts**, removed in the same change that
  registered those hotkeys, and asked for again by a tester on 2026-09-07.
- **A settable velocity key.** Engine side done 2026-09-09, panel side at
  `08aa1ab`; see item 5 under Sequencing. Building it turned up a hole the hardcoding had
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
   built-in but Pro can play loud. The Linear Coarse and Linear Fine naming
   was closed the same day: the owner reports both work in game, so they stay
   as R5 shipped them, see `VELOCITY-CURVES.md`.
3. **Done 2026-09-11.** Pro, from the R5 config. See the Pro section above.
4. **Done 2026-09-09, both halves.** MIDI output, `MIDI-OUTPUT.md`: the
   engine half at `7205e37`, the panel half at `08aa1ab` as a Keystrokes or
   MIDI switch and an output picker in Settings. Nothing has been played into
   a real synth through it yet.
5. **Done 2026-09-09.** A settable velocity key, as
   `VELOCITY_MODIFIER` in `config.json`: alt, ctrl or shift, defaulting to alt.
   The config refuses anything that is not a modifier, because the velocity
   characters are the characters the piano mappings use and a bare velocity key
   would play a note, which is the closed decision in `CONTINUE-HERE.md`.
   `velocity_modifier_conflicts()` names the combinations that collide with the
   selected layout, since choosing ctrl makes fifteen taps play a note in the
   88-key layout and the app is the only thing that can work that out. The
   panel control and the conflict list landed at `08aa1ab`.
6. **Done, both halves.** Drum detection and auto-transpose in the engine on
   2026-09-11, their Settings switches on 2026-09-15.
7. **Done 2026-09-15.** The conversion pipeline: the bundle, the sign-in
   window, the Export menu with the styled and coloured sheets.

Not on this list because they are not shell work: the Wooting and two-device
checks, which need the owner at the keyboard. The duplicate
`calibrate_volume()` in the original window's handler was removed on
2026-09-09; the fix was the order rather than the deletion, because the sweep
that survived a bare deletion would have been the one that runs unfocused.

## Asked for on 2026-09-18

From the owner and a tester in one session. Nothing here is optional.

1. **Rebindable hotkeys, with media keys. Built 2026-09-18, not yet pressed
   in the running app.** Six actions (play or pause, back, forward, stop,
   previous song, next song) under Settings, Hotkeys: a keycap per action,
   click it and press a key, the cross unbinds. The song keys start unbound
   and no media key is a default. `ui/HotkeyNames.hpp` holds the names both
   ways, the keycap text and `HotkeyCapture`. `Action::Hotkey` writes
   `HOTKEY_SETTINGS`, moves a key that another action held, and bumps
   `hotkeyRevision`; the shell loop registers again whenever the snapshot's
   names differ from the ones it asked for. Capture polls `GetAsyncKeyState`
   with the hotkeys unregistered, swallows key-downs in the message pump so
   Escape and Space do not reach Settings, ends when the app loses the
   foreground, and registers only once the captured key is up.
   `HotkeySettings::validate` accepts an empty transport key. Open: a note
   key can be bound, which takes that key from the game while the app runs.
2. **Custom colour themes**, the owner's words: everything customisable in a
   friendly way, text colours included, with the change seen live. A custom
   theme either has a light and a dark version or does not. Without one, the
   light/dark toggle is greyed out while that theme is selected. With one, the
   other version is either customised by hand or chosen automatically, and
   automatic guesses the opposite of the version being worked on. `Skin.hpp`
   says a skin is colour only and `All()` is a fixed array of four whose order
   preferences store by index; both have to give.

   Design, 2026-09-18, in the order it is built:

   - **A theme is named, not numbered.** `ui/ThemeModel.hpp`: a `Theme` has an
     `id`, a `name`, a light and a dark `skin::Skin`, `paired`, and `automatic`.
     Blue and Orange are themes `blue` and `orange`, built in and not editable.
     Preferences store `"theme": id` and `"dark": bool`; an old `"skin": n`
     reads as `n < 2 ? blue : orange` and `n & 1`. A theme id that is gone
     falls back to `blue`. `skin ^= 1` becomes `dark = !dark`.
   - **Unpaired**: one palette, whose own `dark` flag says which it is, and the
     light/dark button is disabled while it is selected. **Paired**: both
     halves; with `automatic` on, the half not on screen is derived from the
     one being edited every time it changes, and editing the derived half by
     hand turns `automatic` off.
   - **The opposite half** keeps each colour's hue and chroma (OKLCH) and takes
     its lightness from the same role in the built-in of the other mode, which
     is what keeps cards above canvas in both. Borders, highlights and shadows
     are translucent black in one mode and white in the other, so they are the
     template's own. The test: the opposite of Blue is within a small
     distance of Blue Dark, role by role, and the same for Orange.
   - **Friendly first, everything second.** Three starting colours, Background,
     Text and Accent, rebuild the whole palette by the same role-lightness
     rule; below them every colour a skin holds is a swatch under its role's
     plain name (Window, Bars, Panels, Controls, Hover, Fields; Text, Secondary
     text, Faint text; Accent, On, Warning, Error; and the derived ones under
     one collapsed row). The app itself is the preview: the theme being edited
     is the active theme.
   - **Stored** in `themes.json` beside `shell-settings.json`, written when the
     editor closes and at exit, colours as `#RRGGBB` or `#RRGGBBAA`. Shape,
     spacing and type never reach the file: a custom theme is colour only.
   - **Settings, Appearance**: the two radios become a list of themes, a
     Customise button and, on a theme of the user's, Delete with a one-line
     confirmation. Customise opens the editor on the chosen theme, or on a
     copy of a built-in, so nothing is disabled and there is no separate New
     or Duplicate; the name is a field at the top of the editor.

   Built 2026-09-18 (`1b062f3`, `f866071`) as above, with one addition: a
   paired theme records which half is its source, so an edit to the derived
   half turns automatic off rather than the next edit to the source undoing
   it. Seen only in the `theme-editor` render: no swatch has been clicked, no
   picker opened and no theme saved from the running app. The swatch and its
   picker are the app's own since `9d0f64f`, seen in the `theme-picker` render
   with no alpha bar in it. Owed: `skin-system.html` knows nothing of themes.
3. **Legit mode** is a reminder carried over from upstream, where it was
   removed for being poor. The owner wants it rethought entirely, not tuned. A
   tester found it laggy.

   **What it is for, the owner's words on 2026-09-18:** make a human
   performance sound played live, not the same each time. Most of what he plays
   is a human recording, and a listener in the game also sees the input timing.
   It is not there to make a poor or unplayable file sound real: no repair, no
   thinning, no playability limits.

   **Built 2026-09-18, `c60e46d` to the commit after `0dc3055`; never heard by
   the owner and never pressed in the running app.** `LEGIT-MODE.md` is the
   account of what was built. It differs from the outline below in three
   places: a moved slider does not read Custom, the chosen player stays
   chosen because it also sets how hard Difficulty bites and whether there
   are hesitations, and choosing it again puts the sliders back; Hand Split
   and Hold Tapped Notes are in Settings and show only when they apply; the
   mini window has Hands and Trigger, unlabelled, and no Speed, which it
   never had.
   Owed: the owner's ear on Pro, Student and Beginner with his recordings; a
   real game with Hands, Hold and Tap, where the tap keys have only met a key
   table; the native suite, which takes the cursor and was not run;
   `skin-system.html` knows none of it.

   **Planned, not started, and not to start until the owner has tested Legit
   mode and says so: Legit mode leaves the app and becomes a mod.** The owner,
   2026-09-18: a build that passes autoplay off as live playing is like AI art
   passed off as hand-drawn, most people do not need it, and the app is smaller
   and cleaner without it. A password or an online unlock locks nothing in open
   source, so the gate is that the code is not in the public source at all.
   - **The test for what leaves**, the owner's: do people who are not trying to
     hide autoplay need it? Speed as a slider: yes, it stays. Hands: yes,
     someone practising plays the other hand themselves; it stays. Everything
     that varies a take, the players, Difficulty, Mistakes, hesitations: no.
     Hold and Tap: no, play and pause already exist and Tap puts a human rhythm
     on someone else's notes; they leave too. Per-song memory goes wherever
     what it remembers goes.
   - **A clean cut:** no Legit word, switch, tag, setting or hotkey row in the
     app. The app keeps one extension point and draws whatever a mod declares:
     its name, sliders, presets, which sliders carry an Estimated mark, and
     its keys. With no mod there is no section.
   - **It is called an add-on, and its folder `addons`,** the owner's point on
     2026-09-18: "mods" reads like Minecraft, a folder people fill with
     whatever they download, and this app loads only what the owner signed.
   - **The add-on** is a DLL in an `addons` folder beside the exe, behind a C ABI:
     the app hands it the score, a seed and the speed and takes back a time, a
     velocity and a skip per event. Tap lives in the playback loop today, so the
     ABI also needs a way for a mod to hold the clock and step it.
   - **Help says it exists,** the owner's, 2026-09-18. First thought was a line
     under About; better, his words, the help button, which is already where
     the app's explaining is meant to go (one button, no text on the controls).
     The entry is framed as the question a user would have, "What is the addons
     folder for?", and its answer is the one place the word Legit stays in the
     app. The question only comes up if the folder is there, so the public zip
     ships an empty `addons` folder after all. Open: what the answer tells
     people to do. "DM me" needs a name, and the standing decision is that
     nothing sent out carries an account name. The help button itself is not
     built and is its own item.
   - **Open, and the owner's to weigh: the licence.** About also says the app is
     based on Zephkek/MIDIPlusPlus under GPLv3. A closed add-on loaded into a
     GPLv3 program is, on the usual reading, part of that program, so whoever
     is given the add-on may be entitled to its source and to pass it on. Not
     giving it to the public is fine under the GPL; keeping it closed among the
     people it is given to may not be. Not legal advice; it needs a real answer
     before the add-on goes to anyone.
   - **Signed.** The app loads only a mod signed with the owner's key, because a
     mod is code and "download this mod" is a malware route. The signature can
     carry a name and an expiry, which is the licence if it goes to many people.
   - **Source** in `D:\Dev\QuartzMIDI-legit`, its own local git, no remote, until
     the owner makes a private repository. The mod's tests go with it; the
     public tests use a dummy mod.
   - **`origin` is public and holds today's Legit commits.** The owner: whatever
     is required so people cannot get it there, and make it clear it is a mod
     people DM him for. That is a rewrite of this branch without the Legit code
     and a force-push, on his yes at the time; it does not reach a clone or
     fork already made. `greasebob/QuartzMIDI` never gets it.
   - Until then nothing Legit is pushed beyond what is there, and the work
     stays as built so the owner can test it.

   **The outline as agreed:**
   - The recording is the performance; Legit mode is a fresh take of it. Every
     offset is a small displacement around what the file already says.
   - A plan is built at song start and on the toggle: a second schedule beside
     `note_buffer`, one offset per event, new seed each start. The dispatch
     thread never sleeps for it, which removes the lag. Offsets may be early.
     The score stays the ground truth for seek, position and duration.
   - The take varies four things: a slow mean-reverting tempo drift, a small
     per-note timing offset, velocity (a phrase-long drift plus a per-note
     part, in steps of the game's 32 levels), and release time. A key is always
     up before its next press.
   - Dropped notes and the random hesitation are cut from the default. Mistakes
     stays as a slider at 0: an inner chord note, never the top or the bottom.
   - Settings: the switch, presets Pro, Student and Beginner, a Difficulty
     slider, and sliders Timing, Tempo, Dynamics, Note Length, Mistakes; a
     moved slider reads Custom. No text. Pro is sized to how much one player
     differs between two takes of the same piece, which is small. A quantised
     file wants a looser preset, and the user picks it; the app does not guess
     the file's kind.
   - The owner, same day: a harder song has more issues live, and how many
     depends on the player we assume, one who practised it a lot or one who
     struggles with it. The presets are that assumed player, named by the
     owner: Pro, Student, Beginner. (The velocity curve once called Pro is
     S-Curve since 2026-09-11, so the name is free.)
     Difficulty is scored (notes per second, chord size, leaps) but it is hard
     to calculate and the user knows the song, so the score is shown as an
     estimate and is only where a Difficulty slider starts: a mark labelled
     Estimated on the track, the handle the user's to move. The profile along
     the song decides where variation and mistakes gather; the slider decides
     how much. Hesitation and falling behind belong to the loosest preset
     only, as plan offsets, bounded and recovered, never a sleep.
     The preset, Difficulty and the sliders are remembered per file, the
     owner's yes on 2026-09-18, behind a switch of its own so a user can keep
     one setting for every song instead; nothing in the app is saved per song
     today, so this is new plumbing.
   - The owner, later the same day: Legit mode also carries the ways a person
     plays along, and stays open to what testers ask for next. Not built:
     - **Hands:** Both, Right, Left. By track when the file has two; otherwise
       an estimated split the user can move, shown as an estimate like
       Difficulty. The app never presses or releases a key of the silent
       hand, so the user can play that hand themselves.
     - **Trigger:** Auto, Hold, Tap. Hold plays while a bound key is down and
       lifts every key and the pedal when it comes up. Tap plays the next
       note or chord per press, the chord keeping its own spread and
       velocities; two keys may share the action, since one key cannot be
       tapped as fast as a run. The keys bind in Hotkeys. Note length in Tap
       is an option, the owner's ask: held as long as the key is, or the
       recording's own length. The recording's pedal applies in both.
     - Left to Claude by the owner: Hands and Trigger sit on the main panel
       beside Speed, since they change per song and mid-song; the player
       preset, Difficulty and the sliders stay in Settings.
     - **Speed is a slider by default**, the owner's words: for everyone, not
       a Legit mode control, 0.25 to 2 with the value beside it, and the minus
       and plus buttons go. The speed hotkeys still step it.
       Today a speed change rewrites every event's time
       (`ui/ShellEngine.cpp:402`), which a drag would do every frame; with the
       plan schedule speed becomes a rate on the clock instead.
   - Old `LEGIT_MODE_SETTINGS` keys still load and are ignored.

   **Measured 2026-09-18** with `python tools\measure-midi.py x64\Release\midi`
   on eight of the owner's recordings, one each from Chewie Melodies,
   ViddyWell, The Flaming Piano, Theishter, FrankTedesco's KOFI folder and SL K
   out of `D:\MIDI++ 1.0.4.R5 Release\midi`, plus the two already there:
   - All eight are played: 0 to 6% of onsets sit on a 1/48 grid.
   - Median chord spread is 9 to 20 ms in seven files and 0 in the Chewie
     file, so a take's per-note offset has to stay well under that, a few
     milliseconds, or it rewrites the player's chords.
   - Chords whose notes share one tick are 0 to 15% in seven files and 81% in
     the Chewie file. Same-instant presses are in real recordings; Legit mode
     does not forbid them.
   - 2 to 26% of distinct onsets are under 3 ms apart, which is what the
     dispatch batch could merge. Not yet measured on the playback side.
   - Velocities use 19 to 31 of the game's 32 levels, so one level is a
     meaningful Dynamics step.

   **First, before the model:** measure whether plain playback flattens those
   close onsets (one batch fires everything due together), with
   `LatencyTests.exe` and these files.

   **Proof:** the existing `--legit` tests, plus dispatch lateness on equals
   off, two seeds differ, a fixed seed repeats, offsets stay inside their
   bounds with the span preserved, and the humanised take exported as a `.mid`
   for listening outside the game.
4. **AutoVol's keys.** `HOTKEY_SETTINGS` carries `VOLUME_UP_KEY` and
   `VOLUME_DOWN_KEY` and the live path in `MIDI2Key.cpp` hardcodes the arrows.
   A tester will name a piano with other volume keys if they find one.
5. **Native network MIDI.** rtpMIDI already gives a session a Windows port, so
   this waits on the tester saying what sends the MIDI and which way it flows.
6. **Themes that change the whole design, and can be shared.** The owner's,
   2026-09-18, and nobody has asked for it: the testers have been busy and
   short on feedback, and the owner expects the look to be the one thing people
   want to make their own. It reopens item 2's "colour only". Not started; it
   comes after Legit mode is tested and split.
   - A skin already holds more than colour (`MIDI++/Skin.hpp`): radii, spacing
     and padding, control heights, the type scale and shadows. A custom theme
     edits only the colours today.
   - The theme editor gains shape, spacing, size and shadow beside the
     colours, with the app redrawing as they move, as it does for colour.
   - Every value is clamped to a range where the layout still fits, at the
     minimum window and at 100 to 200 percent, so no theme can break the app.
     The render tests hold those ranges.
   - Themes get Import and Export as one file. It is data, so it is safe to
     pass round; this is the app's answer to mods, which as code would be a
     malware route, would need the owner to review and sign each one, and
     would let anyone share a Legit clone.
   - Out of reach without code, and left alone: moving or removing panels,
     other icons, fonts the app does not ship.
7. **A help button.** The owner's, 2026-09-18: one button, and behind it the
   questions people actually have, each framed as the user would ask it. It is
   where the app's explaining goes, since the controls carry none. Not started.
   - Each entry is first a defect report: if a control can be made to answer
     the question itself, fix the control and leave the entry out
     (`HANDOFF.md` section 15). What is left is what no control can say.
   - Wording is the owner's voice: a question, then one or two sentences.
   - **In folders,** the owner's, 2026-09-18, so someone past the basics goes
     straight to what is new. Sections that open, as Settings has them:
     The owner turned down a folder called "New", and a folder per tool: the
     categories can be better, and every name plain and professional, as the
     rest of the app's are. Proposed, not settled, by what the person is
     trying to do, since that is how they arrive:
     - **Getting Started**: opening a file, Play and the hotkeys, where the app types,
       61 or 88 keys. The tour's replay button sits here.
     - **Playback**: speed, transpose, Hands, tracks and Solo Piano,
       shuffle, the countdown.
     - **Live Input**: choosing an input, Kernel Streaming and WinMM,
       MidiConnect, Wooting, the channel.
     - **Velocity and Output**: velocity hotkeys, the curve editor, the sustain
       cutoff, AutoVol, MIDI output.
     - **Conversion and Sheets**: the converter and its sign-in, sheets, where files go.
     - **Appearance and Controls**: themes, hotkeys, key mapping, mini, opacity.
     - **Troubleshooting**: it typed into my browser, extra notes, a greyed
       hotkey, a stuck key, nothing plays.
     - **Add-ons**.
     - What a build added is not a folder. Each entry carries the build it
       arrived in; a folder holding one shows a mark; and beside the search
       field sits one chip named for the build, "Added in 2.1", that filters to
       those entries. After an update Help opens once with the chip on. So
       nothing is named New and an entry never has to move.
     - A search field at the top over every question, like the file list's.
   - Candidates, from what testers have tripped on and what this page records:
     - Why did it type into my browser? (it types into whatever has focus, and
       the lowest 88-key notes are `ctrl+` combinations, so `G#1` is `ctrl+w`)
     - Why do I hear extra notes with Velocity on? (the velocity keys are the
       note keys; the game's script has to take the modified keypress)
     - 61 or 88 keys, and what does folding out-of-range notes do?
     - Which MIDI input do I pick, and what is Kernel Streaming?
     - What is MidiConnect?
     - What does AutoVol do, and why does it want calibrating?
     - What does the sustain cutoff change?
     - Why is a hotkey greyed out? (another program holds that key)
     - Why does Solo Piano leave some tracks playing, or mute a piano?
     - What do Hands, and the Estimated mark on a slider, mean?
     - Where do my sheets and converted files go?
     - Why does the converter ask me to sign in?
     - What is the addons folder for? (item 3)
   - Open: where the button lives (the top strip beside Settings is the
     obvious place), and whether it is a popover like Settings or its own
     window in mini.
   - **A tour on first open,** the owner's, 2026-09-18: it highlights one
     control after another and says what each is, the highlight gliding
     smoothly from one to the next, with Skip always there, and a button
     inside Help plays it again. Not started.
     - How: a dimmed layer over the window with a cut-out round the control and
       a small card beside it (the text, Back, Next, Skip, and which stop of how
       many). Each stop's control records its own rectangle when it is drawn,
       so the tour follows the layout at any size and DPI, and the cut-out and
       the card ease between rectangles. The shell draws on demand, so the
       tour keeps frames coming while it moves, as the hotkey capture does.
     - Seen once: a flag in the preferences file; Skip and the last stop both
       set it. Escape skips. It never starts over a song that is playing.
     - Short, or it gets skipped: eight stops at most. Candidates: the file
       list; Play and the key legend, which is where to say that the app types
       into whatever has focus, so the hotkeys are for when the game has it;
       Speed and Transpose; Hands; Tracks and Solo Piano; the state pills; the
       MIDI input pill; Settings and Help. That stop is the answer to the
       warning question left open under "Reported by testers".
     - Wording is the owner's voice, one sentence a stop.
     - Not the "Before you play" modal removed in `c9ea480`, the owner's point:
       that was a block of text put in front of everyone because a warning
       seemed owed, with nothing to do but dismiss it. The tour is designed
       for both readers. Someone new is shown the app control by control,
       where each thing is; someone experienced presses Skip once, never
       sees it again, and can play it back from Help when a control is new.
       And the app is a different app. When the modal went in there was
       nothing to explain: it was MIDI++ in a new skin. Now it is several
       tools in one window (autoplay, live input, MidiConnect, MIDI out, the
       converter, sheets, the velocity editor, themes, hotkeys, Hands) behind
       controls that did not exist then, so there is something to show.
     - Mini has its own layout: the tour runs in the full window and leaves
       mini alone. The render tests get a scenario per stop.
