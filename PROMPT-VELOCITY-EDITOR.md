# Prompt: the velocity editor rewrite

Written 2026-09-09 for the panel seat. Paste the block below. It is
self-contained on purpose: it names the seam, the files, and what the other
seat is doing at the same time, because the one time that was left implicit
both seats built the same transport engine and one was thrown away.

---

You own `ui/`. You are rebuilding the velocity curve editor in the MIDI++ ImGui
shell, at `D:\Dev\MIDIPlusPlus-modded`, branching from
`claude/velocity-graph-parametric`. C++20, MSVC, ImGui. Build with
`tests\run-shell-tests.ps1` and `tests\run-shell-parity-mutations.ps1`, both of
which stub injection to an in-process recorder and are safe to run any time.
Do not run `tests\run-native-tests.ps1` or `tests\run-latency-tests.ps1`
without asking: they take the real cursor and type into whatever has focus, and
the owner games on this machine.

## The seam, which is not negotiable

You own `ui/`, including `ShellEngine::Action` and `EngineSnapshot`. If you
need a new action, add it. Claude owns `MIDI++/` and `tests/`, and is not
editing `ui/` while you work on this. The one file where you will meet its
recent work is `ui/VelocityModel.hpp`, described below. Read
`midiplusplus-astra-claude-split` in the project memory if you have it; the
short version is that directory ownership is too coarse for shared symbols, so
the panel owner owns them and everyone else consumes.

## What already landed, and what you build on

`ui/VelocityModel.hpp` now has `VelocityCurveAt(preset, x)`. The engine stores
a table of 32 thresholds where bucket `i` takes every input up to
`thresholds[i]`, so the curve passes through `x = thresholds[i]/127` against
`y = i/31`. `VelocityCurveAt` reads it that way. The old `VelocitySamples`,
which resampled onto a uniform 32-point grid and drew Linear Fine's last
interval flat, is deleted. Do not bring a uniform resample back:
`VelocityCurveDrawingTests` in `tests/ShellTests.cpp` and the
`curve-resampled-on-uniform-grid` mutation both exist to stop it.

`VelocityThresholds(preset, edit)` converts an edited curve back into the 32
thresholds the engine consumes, and returns built-ins byte-identical when
nothing has been edited. Keep that property.

## What is wrong today

`HANDOFF.md` section 12 calls this the most important UI problem in the
project, and answers it: edit a smooth curve, sample it to 32, and draw the
steps as a faint ghost underneath. The build does the opposite. The 32 bars are
the editing surface, at `ui/Panels.cpp:824`, and `Panels.cpp:842` clamps each
bar between its two neighbours' current heights, so on a near-linear curve one
bar can travel about one step in 31. Making an audible change means dragging
all 32 in order, each unlocking a sliver for the next. The clamp is not wrong
about monotonicity; enforcing it by pinning the handles is what makes the
handles useless.

Hiding the bars under Advanced is not the fix. Manual editing still has to be
worth using, it just is not the primary interface.

## What to build

All of the following. None of it is optional and none of it is a later phase.
Sequencing is yours; scope is not.

1. **Anchor points, as Photoshop's Curves works.** Click the line to drop an
   anchor, drag it and the curve bends smoothly through it, drag it off the
   graph to delete it. Three or four anchors should cover nearly every response
   anyone wants, and one drag should change a region of the player's range
   rather than one bucket in 32.

2. **Monotone cubic interpolation between anchors, PCHIP.** This is what
   retires the clamp: monotonicity comes from the interpolation rather than
   from restricting the mouse. The curve must not fold backwards whatever the
   anchors do.

3. **Free draw as a second mode, not a someday.** Hold and sweep across the
   graph, the line follows the cursor, and it smooths on release. Faster than
   anchors for a big reshape, worse for precision, which is why both exist.
   Four things it has to get right, all the same principle as retiring the
   clamp, which is to leave the hand alone and legalise the result:
   - Only what was swept changes. Draw over the middle third and the ends keep
     their shape, blended at the join rather than stepping.
   - Smoothing happens on release, not during. The line follows the cursor
     exactly while drawing, because a line that fights the hand feels broken,
     and a light pass afterwards stops hand jitter becoming 32 jagged buckets.
   - Monotonicity is repaired, not enforced. A sweep that dips, or runs right
     to left, must be allowed to happen and then be made non-decreasing on
     release. Blocking the cursor is what makes the current bars miserable.
   - Undo and redo. A sweep replaces a whole region in one gesture, so there
     has to be a way back.

4. **One representation, not two.** Draw produces a curve, anchors edit a
   curve. A shared model is what makes sweeping a rough shape and then pulling
   an anchor to refine it one continuous piece of work instead of a mode switch
   that discards the last one. The model is a smooth curve sampled to 32, per
   section 12, never 32 stored values.

5. **The 32 bars stay as the ghost readout** section 12 asks for, drawn faint
   underneath. They are visible, not the editing surface.

6. **Anchors snap to the played-velocity band.** The histogram of what the
   player actually played is already drawn and wired to `velocity_telemetry`.
   Section 12 opens by saying the user cannot tell what to aim for, and a snap
   target is that answer made concrete.

7. **Undo and redo are the primary control, and A/B moves to Settings.** This
   revises `HANDOFF.md` section 12 point 4, agreed 2026-09-07. A/B works and
   stays a release longer as a hidden option, so we learn whether its pinned
   reference is missed. It just stops holding a button in the editor.

## House rules, which are not stylistic preferences

- No em dashes in UI copy or docs.
- Delete any text that only restates the control it labels.
- Icons for utility controls, never single letters, never font glyphs.
- Colour is never the only signal for a state.
- Accent is selection, focus, slider fills and the live curve. Not decoration.
- Do not invent behaviour and describe it as existing. If you are unsure
  whether something is in the code, say so.

## What done looks like

The existing suite still passes, including `VelocityCurveDrawingTests` and all
ten parity mutations. New tests cover the model rather than the pixels: PCHIP
stays monotone for adversarial anchor sets, a partial sweep leaves the
untouched region unchanged, a backwards sweep is repaired rather than blocked,
undo and redo round-trip, and an unedited built-in still converts back to its
exact thresholds. Add a render-test scenario for the editor so the panel is
covered the way the others are.

Then say plainly which of the seven items are in and which are not. Do not
report it finished if one is missing.
