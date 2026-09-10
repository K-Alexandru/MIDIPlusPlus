# The MIDI output panel, and the velocity modifier control

Addressed to the panel seat. Everything below is the prompt, with nothing
addressed to the owner mixed in.

Two pieces of panel work whose engine halves are already built and tested.
Neither is reachable by any user until this exists, which is why they are one
job rather than two.

## Where you are working

You own `ui/` in the MIDI++ ImGui shell at `D:\Dev\MIDIPlusPlus-modded`. C++20,
MSVC, ImGui, GPLv3.

Start from `claude/velocity-graph-parametric`, which carries both engine halves
and your velocity editor:

    git fetch origin
    git checkout -b output-panel origin/claude/velocity-graph-parametric

Build and test with `tests\run-shell-tests.ps1 -Render` and
`tests\run-shell-parity-mutations.ps1`. Both stub injection to an in-process
recorder and are safe to run at any time. Do not run
`tests\run-native-tests.ps1` or `tests\run-latency-tests.ps1` without asking
first: they take the real cursor and type into whatever window has focus.

## The seam

You own `ui/`, including `ShellEngine::Action` and `EngineSnapshot`. Both
engine halves deliberately added nothing to either type, so the lists below are
yours to write once rather than twice. The other seat owns `MIDI++/` and
`tests/` and is not editing `ui/` while you work on this.

## Piece one: the MIDI output route switch

`MIDI-OUTPUT.md` is the spec and it is current. Read its "State, 2026-09-09"
section first, then "What the panel half adds", which lists the exact members to
add. The engine half gives you:

- `VirtualPianoPlayer::OutputTarget`, `Keystrokes` or `MidiDevice`.
- `set_output_target()`, which releases everything held on the outgoing target
  before it stores the new one. Call it rather than writing the atomic, because
  that ordering is the whole point and a note held across a bare store is
  stranded either in the game or on the synth.
- `open_midi_output(id)`, `close_midi_output()`, `opened_midi_output()`.
- `EnumerateMidiOutputs()`, which returns the same `MidiInputDevice` rows the
  input picker already draws, so it is the same picker with a different source.

What the panel owes:

- A two-way choice next to the existing MIDI input picker, keystrokes or MIDI.
- The device combo disabled while Keystrokes is selected.
- **The Velocity control reading as unavailable rather than off** when MIDI is
  the target, because velocity is still being sent, just in the note-on byte
  instead of as keystrokes. Off would be a lie about what the app is doing.
- Closing the port and losing the device both already fall back to keystrokes
  in the engine, so the panel has to show that rather than keep displaying a
  device that is gone.

## Piece two: the velocity modifier

`VELOCITY_MODIFIER` in `config.json` is now a setting: `alt`, `ctrl` or
`shift`, defaulting to `alt`. Alt+1 is Roblox's capture shortcut, which is why
velocity ships off by default, and this is the control that lets someone move
it.

The engine gives you `velocity_modifier_conflicts()`, which returns the
combinations that collide with the layout currently selected, spelled the way
the mapping spells them, such as `ctrl+w`.

What the panel owes:

- The control itself, wherever the velocity settings live.
- **The conflict warning, which is the point.** Choosing ctrl is legal and
  makes fifteen taps play a note in the 88-key layout, because that layout
  binds its lowest notes to ctrl+ combinations. The engine can work that out
  and cannot say it. A user who picks ctrl and gets phantom notes has been
  handed a footgun by us.
- The warning has to follow the selected layout, because the same choice is
  clean in the 61-key layout. `velocity_modifier_conflicts()` already does
  that; do not cache its answer across a layout change.
- Colour is never the only signal for a state, so the warning says something.

## House rules

These are not stylistic preferences.

- No em dashes in UI copy or docs.
- Delete any text that only restates the control it labels.
- Icons for utility controls, never single letters, never font glyphs.
- Colour is never the only signal for a state.
- Accent is selection, focus, slider fills and the live curve, not decoration.
- Do not invent behaviour and describe it as existing. If you are unsure
  whether something is in the code, say so.

## What done looks like

The existing suite still passes, including `MidiOutputTests`,
`VelocityModifierTests` and all fourteen parity mutations.

New tests cover the dispatch rather than the pixels: selecting MIDI routes
through `set_output_target` rather than storing the atomic, a device that fails
to open leaves the app on keystrokes, losing the port is reflected in the
snapshot, and the conflict list shown follows a layout change. Add render
scenarios for both controls so they are covered the way the editor now is.

Then say plainly what is in and what is not.
