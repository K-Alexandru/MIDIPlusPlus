# Paste-ready prompt for another assistant

Use this when handing work to something that does **not** have this machine, the
repo, or a compiler. Everything below is self-contained.

Pick the task at the bottom, delete the others, and paste the whole thing.

Current as of `08d07b4`. Tasks A and B from the earlier version of this file are
done; only the ImGui shell is still worth delegating. Anything that has to
compile against the real tree should stay on the machine that has it.

---

## Context to paste

I am rebuilding a Windows MIDI-to-QWERTY app for virtual piano games, forked
from Zephkek/MIDIPlusPlus (C++20, MSVC, Win32 + GDI+ today, GPLv3). It converts
live MIDI input into keystrokes and also autoplays MIDI files as keystrokes.

Current state:

- **Input path is done.** One interface, `IMidiInput`, with `enumerate` /
  `open` / `close` and a callback of `(uint64_t timestampQpc, const uint8_t*
  data, size_t len)`. Three backends: WinRT (`Windows.Devices.Midi`), WinMM
  (via RtMidi), and Wooting analog keys read straight from the Wooting analog
  SDK, bound at runtime with `LoadLibraryW`. Devices are identified by an
  opaque string id, never an index. loopMIDI note on/off, sustain and velocity
  pass through WinRT and WinMM to the keyboard hook. Physical playing and
  delivery into an actual game remain unverified.
- **Timing instrumentation exists.** An opt-in `WH_KEYBOARD_LL` hook on its own
  message thread, a header-only bounded MPSC ring, and a report window. It
  measures from the backend callback to the hook, which is *before* the game
  processes anything and *after* the transport has already done its work, so it
  cannot compare WinRT against WinMM and says nothing about audio latency.
- **Legit mode exists.** Humanised autoplay applied at dispatch, not baked into
  the parsed score, so it toggles mid-song and leaves seek exact.
- **The UI is a mockup only.** No ImGui code exists. The agreed direction is
  Dear ImGui with one layout engine and four skins (Classic, Classic Dark,
  Modern, Modern Dark) as parameter sets, plus a mini mode.
- **Known and unfixed:** velocity still goes out as an ALT+key tap. Both the
  live and the autoplay path now send the same four events on a changed bucket
  and skip unchanged ones, so a plain note-on costs 1 event with velocity off
  and 5 on a change. Whether the target game accepts velocity without ALT at all
  is unknown and would take that 4 down to 1. An ALT cause for upstream #44 and
  a per-event game frame penalty are hypotheses, not verified facts.
- **Also unfixed:** `InputInjector.cpp` calls `NtUserSendInput` through a
  hand-built syscall thunk whose syscall number is scraped at runtime; if init
  fails it can silently no-op every keystroke. Its speed benefit has not been
  measured. The instrumentation reports impossible and partial return counts; it
  does not replace this backend.

Constraints that matter:

- Injection must never share a thread with the message loop. Upstream admits
  window dragging conflicts with the injection syscall.
- The app sits beside a running game, so idle cost matters.
- GPLv3: any code you suggest must be compatible and attributable.

House rules for anything user-facing:

- No em dashes in UI copy or docs.
- Delete any text that only restates the control it labels.
- Icons for utility controls, never single letters, never font glyphs.
- Colour is never the only signal for state.
- Do not invent behaviour and present it as existing. If you are unsure whether
  something is in the code, say so rather than describing it.

---

## Task: ImGui shell skeleton

Write the skeleton for a Dear ImGui + DirectX 11 single-window app with:

- a `Skin` struct of runtime values (not `constexpr`) covering five surface
  tones (canvas, structure, card, elevated, recessed), two shadow definitions,
  border alpha, four radii, a 4px spacing scale, control height and three type
  sizes, with four instances for the skins named above
- helpers that draw a raised panel and a recessed field using that struct, with
  the ambient shadow approximated as two or three stacked translucent rounded
  rects, a one-pixel darker contact line, and a one-pixel lighter top highlight
- a docked layout with a primary strip, a left file list, and a right column

Do not write the feature panels. I want the shell and the skin system only.

The layout to build against is `skin-system.html` in the repo, which is an
operable mockup rather than a picture: its measurements are real. Classic is
1090 x 635 px with the velocity editor collapsed, Modern 1090 x 728. The primary
strip is 81 px. Key mapping is a separate 840 px window. Settings owns the MIDI
transport chooser; the strip carries only what changes while you play.
