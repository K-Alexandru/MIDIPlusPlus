# Start here

Entry point for whoever picks this up next, assistant or human. Written
2026-09-04 at `input-path-r5`.

Read this first, then `HANDOFF.md` for the full brief. `LATENCY.md` and
`LEGIT-MODE.md` cover the two subsystems built in this session.
`HANDOFF-PROMPT.md` is a self-contained block to paste into an assistant that
does **not** have this machine.

## What this is

A Windows MIDI-to-QWERTY app for virtual piano games, forked from
Zephkek/MIDIPlusPlus. C++20, MSVC, Win32 + GDI+, GPLv3. It turns live MIDI into
keystrokes and autoplays MIDI files as keystrokes.

## State

**The input path is finished and validated.** Notes were confirmed reaching a
real game on 2026-09-04. That was the question the whole WinRT port was blocked
on, so the port stays. Everything below it is settled:

- `IMidiInput` fronts WinRT, WinMM and Wooting analog. Devices are opened by an
  opaque id, never an index.
- Timing instrumentation exists and is honest about what it does not measure.
- Legit mode exists, works mechanically, and sounds wrong. Demoted to a Config
  checkbox. See the verdict in `LEGIT-MODE.md`.
- Autoplay and live input now send the identical four-event ALT velocity tap.

**The UI is the blocker.** It is the original Win32 + GDI+ window. The agreed
replacement is Dear ImGui, and no ImGui code exists yet.

## Build and run

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'MIDI++.sln' /p:Configuration=Release /p:Platform=x64 /m
```

Output is `x64\Release\MIDI++.exe`. Run it **from that directory**: it reads
`config.json` and scans `midi\` relative to the working directory.

Tests need a loopMIDI port. `run-latency-tests.ps1 -ListPorts` names them.

```powershell
& .\tests\run-latency-tests.ps1 -LoopbackPort 'MIDI'   # full suite, needs loopMIDI
& .\tests\run-latency-tests.ps1 -Legit                 # no hardware needed
```

## The next job: the ImGui shell

This is weeks of work, and it is the only large thing left.

`skin-system.html` is the spec. It is an operable mockup, not a picture: open it
and press things. Its measurements are real and were taken from the rendered
page, so build against them rather than guessing.

- Classic is 1090 x 635 px with the velocity editor collapsed, Modern 1090 x 728.
- The primary strip is 81 px.
- Key mapping is a separate 840 px window, because it is set up once and should
  not hold height in the window you keep open.
- Settings owns the MIDI transport chooser and its latency reading. The strip
  carries only what changes while you play.
- The velocity editor starts collapsed for the same reason.

**`MIDI++/Skin.hpp` already exists and is the starting point.** It is the four
skins as runtime data, extracted from the mockup, with no ImGui, Win32 or GDI+
dependency, so it compiles anywhere and can be unit tested. It carries the five
surface tiers, the shadow definitions, the border alphas, the concentric radii,
the 4px spacing scale, control heights and the type scale. What it does **not**
carry is the drawing: the panel and field helpers that turn those numbers into a
draw list are the first thing to write.

Order I would work in:

1. Vendor Dear ImGui plus the DX11 and Win32 backends. None of it is in the tree.
2. A window that opens, clears, and applies `skin::Classic()` to `ImGuiStyle`.
3. `RaisedPanel()` and `RecessedField()` helpers using `Skin`. The ambient shadow
   becomes two or three stacked translucent rounded rects, the contact shadow a
   one-pixel darker line under the surface, the top highlight a one-pixel lighter
   line inside it. Get these right before anything else: every panel uses them.
4. The layout shell only: primary strip, left file list, right column. No
   feature panels yet.
5. Then port panels one at a time against the mockup, starting with Tracks.

Hard constraint from `HANDOFF.md` section 4: **injection must never share a
thread with the message loop.** Upstream admits window dragging conflicts with
the injection syscall. This is a reason the UI is being rewritten, so do not
reintroduce it.

## House rules

From `HANDOFF.md` section 15, and they are not stylistic preferences:

- No em dashes in UI copy or docs.
- Delete any text that only restates the control it labels.
- Icons for utility controls, never single letters, never font glyphs.
- Colour is never the only signal for a state.
- Accent is selection, focus, slider fills and the live curve. Not decoration.
- Do not invent behaviour and describe it as existing. If you are unsure whether
  something is in the code, say so.

## Things that are settled, so do not redo them

- **Do not move velocity off ALT.** The velocity keys are
  `1234567890qwertyuiopasdfghjklzxc` and the piano mappings cover the same
  characters, so a bare velocity key would play a note. ALT is the only thing
  separating "set velocity" from "play a note". Closed.
- **Do not apply legit mode at parse time.** `LEGIT-MODE.md` has the three
  reasons and the bugs the parse-time version had.
- **Do not present the timing numbers as end-to-end latency.** They stop at the
  keyboard hook, which is before the game and after the transport.

## Still open, in rough order of value

1. **The ImGui rewrite.** Above.
2. **Legit mode, second attempt**, only if humanised autoplay matters. The model
   is what is wrong, not the code: three uniform draws with no memory make
   noise, while human microtiming has long-range 1/f correlation and follows
   musical structure. Start there.
3. **Wooting analog playing.** The backend has never been exercised with the
   hardware. The owner has a Wooting, so this is testable.
4. **Two MIDI devices at once.** The id-based opening in `269c2f2` is meant to
   fix this and has not been confirmed with two inputs present. Two loopMIDI
   ports reproduce it without a second piano.
5. **`MIDIConnect`'s 6.5MB table** and **the `NtUserSendInput` syscall stub**,
   both described in `HANDOFF.md` section 4. The stub is the riskier of the two:
   its uninitialised state returns 69, which silently no-ops every keystroke.
6. **YouTube to MIDI pipeline.** Lowest coupling, can happen anytime.

## Repository note

The only git remote is **upstream** (`Zephkek/MIDIPlusPlus`), not a fork. Ten
commits of work sit on the local `input-path-r5` branch and exist nowhere else.
`main` is still back at `e37ba7e`. Adding a personal remote and pushing is the
single highest-value five minutes available, and nobody has done it yet.
