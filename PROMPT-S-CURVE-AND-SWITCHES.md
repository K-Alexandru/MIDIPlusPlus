# S-Curve in the mockup, and two Settings switches

Addressed to the panel seat. Everything below is the prompt, with nothing
addressed to the owner mixed in.

## Where you are working

You own `ui/` in the MIDI++ ImGui shell at `D:\Dev\MIDIPlusPlus-modded`. C++20,
MSVC, ImGui, GPLv3.

Start from `claude/curves-pro-drums`, which carries the engine halves below:

    git fetch origin
    git checkout -b s-curve-switches origin/claude/curves-pro-drums

Build and test with `tests\run-shell-tests.ps1 -Render` and
`tests\run-shell-parity-mutations.ps1`. Both are safe to run at any time. Do
not run `tests\run-native-tests.ps1` or `tests\run-latency-tests.ps1` without
asking first: they take the real cursor and type into whatever has focus.

## The seam

You own `ui/`, including `ShellEngine::Action` and `EngineSnapshot`. The engine
halves added nothing to either type. The other seat owns `MIDI++/` and `tests/`
and is not editing `ui/` while you work on this.

## Piece one: rename Pro to S-Curve in the spec

The owner renamed the tuned curve from Pro to S-Curve on 2026-09-11. The engine
already ships it under that name as the sixth built-in,
`midi::VelocityCurveType::SCurve`, so the shell's preset list reads "S-Curve"
with no change from you.

The mockup still says Pro:

- `skin-system.html:1069`, the decisions table row.
- `skin-system.html:1097`, the preset entry `{n:"Pro", ...}`.
- `skin-system.html:1100`, the comment.

Rename all three. Keep the "recommended" tag unless the owner says otherwise.
The mockup's `x*x*(3-2*x)` shape is a stand-in, not the real values; the real
32 values are in `PlaybackCore.cpp`. Then regenerate the captures with
`tools/capture-mockup.ps1` so `docs/design/` matches.

## Piece two: Settings switches for drum detection and auto-transpose

Both are honoured by the engine now and reachable only by editing
`config.json`. `SHELL-GAPS.md`, "Drum detection and auto-transpose", has the
details.

- **Drum detection** reads `midi::Config::getInstance().midi.DETECT_DRUMS` at
  Load. A detected track gets `TrackRow::drums`, stops counting as piano, and
  Solo Piano mutes it. The file key is `MIDI_SETTINGS.DETECT_DRUMS`, default
  true.
- **Auto-transpose** reads `configJson["AUTO_TRANSPOSE"]["ENABLED"]` at Load and
  sets `state.transpose` to the suggestion, clamped to plus or minus 12. It
  never sends an arrow key. Default false.

What to add:

- An `EngineSnapshot` field and an `Action` for each switch.
- Each handler writes the file key through `configJson`, then `touchConfig()`
  and `flushConfig()`. The drum handler must also set the `Config` singleton,
  because that is what `process_tracks` reads.
- Both take effect at Load. If a file is loaded, reload it so the change shows
  at once, rather than leaving the track list describing the old setting.
  Next and Previous already fall through into `Action::Load`; reuse that path.
- Settings copy says what each switch does, not what it is called. Follow the
  house rules in `CONTINUE-HERE.md`: no em dashes, and nothing that only
  restates the label.

Add a render scenario that shows both switches at every skin and DPI. Add a
mutation for each handler in `run-shell-parity-mutations.ps1`, following the
existing `ui\` cases.

## Piece three: the export menu

`Copy as sheet` is the primary button in Playback and should not be. It becomes
a menu, as `SHELL-GAPS.md` asked in the owner's UI pass. The engine now has the
formats for its entries, in `MIDI++/SheetExport.hpp`:

- **Copy as sheet.** Unchanged: `sheet::ToVirtualPiano`, as `Action::CopySheet`
  builds it today.
- **Copy styled sheet.** `sheet::Style(...).text`. This is midi-converter's
  notation: braces for spread chords, out-of-range notes kept, tempo comments
  and a line per bar.
- **Save coloured sheet.** `sheet::ToHtml(sheet::Style(...))`, written as an
  `.html` file beside the MIDI file or through a save dialog. Say where it went.

Feed `sheet::Style` like this:

- **Notes:** the same audible note-ons `Action::CopySheet` collects, as
  `sheet::TimedNote{seconds, midi}`. `MidiNumberForNoteName` in
  `MidiOutput.hpp` turns the event's name back into a number.
- **Tempo and meter:** `sheet::TempoMarksFromTicks` and
  `sheet::MeterMarksFromTicks`, from `midi_file.tempoChanges`, the file's
  time-signature events and `midi_file.division`.

Every field of `sheet::StyleOptions` is a setting midi-converter exposes, so
all of them are owed a control. Nothing is optional. Group them under the menu
or in Settings, whichever reads better; that call is yours.

Two midi-converter features have no engine half, because they are UI:

- **Image export.** It screenshots the rendered sheet. Render the coloured
  items with ImGui to a PNG, or leave it to the HTML page, which prints and
  screenshots cleanly. Pick one and say which.
- **Per-region transposition.** A user selects a run of chords and transposes
  just those. `sheet::StyledItem` keeps each chord's position, so a selection
  can be mapped back to notes.

The notation is MIT-licensed from ArijanJ. Credit it in the About box next to
the other third-party notices. The build already copies its licence beside the
executable as `midi-converter-LICENSE.txt`.
