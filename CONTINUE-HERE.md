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

**The UI is the blocker.** The shipped app is still the original Win32 + GDI+
window. The Dear ImGui replacement now has a working shell (see below), but no
feature panel has been ported and the two do not share code yet.

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

The ImGui shell is a separate project and a separate executable:

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'ui\MIDIShell.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
```

Output is `build\shell\MIDIShell.exe`.

## The next job: finish the ImGui UI

The shell is built. Porting the panels onto it is the remaining large job.

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

**The shell exists and builds.** `ui\MIDIShell.vcxproj` produces
`build\shell\MIDIShell.exe`: a DX11 + Win32 ImGui window that applies any of the
four skins, draws the primary strip, the left file list and the right column,
and carries a skin picker so the four can be compared the way the mockup
compares them. Dear ImGui (docking branch) is vendored under `third_party/`.
It is a **separate executable on purpose**: the working app is never broken by
UI work in progress, and nothing in it links against `PlaybackCore` yet.

What it does not have: real fonts (it is still on ImGui's default bitmap font,
so Segoe UI and IBM Plex Sans are not loaded), DPI awareness, and any feature
panel at all. Those are the next three jobs, in that order.

**`MIDI++/Skin.hpp` is the data behind it.** It is the four
skins as runtime data, extracted from the mockup, with no ImGui, Win32 or GDI+
dependency, so it compiles anywhere and can be unit tested. It carries the five
surface tiers, the shadow definitions, the border alphas, the concentric radii,
the 4px spacing scale, control heights and the type scale. What it does **not**
carry is the drawing: the panel and field helpers that turn those numbers into a
draw list are the first thing to write.

Order to work in from here:

1. ~~Vendor ImGui, open a window, apply a skin, write the raised and recessed
   helpers, lay out the shell.~~ Done.
2. **Fonts.** Load Segoe UI for Classic and IBM Plex Sans for Modern at the
   sizes in `Skin::type`. The default bitmap font makes everything look wrong
   and hides real spacing problems, so do this before judging any layout.
   **Attempted and reverted 2026-09-04, so read this first:** the obvious
   `io.Fonts->AddFontFromFileTTF(...)` after backend init, plus `PushFont` and
   `PopFont` around the frame, compiled cleanly and then rendered a completely
   blank window. The vendored ImGui is 1.93 WIP, whose font system was rewritten
   and whose `PushFont` now takes a size argument, so the single-argument form
   is not the call it used to be. Either pin an older ImGui or read the 1.92+
   font migration notes before trying again. IBM Plex Sans is not a Windows font
   and was not installed on the dev machine, so it needs shipping or a fallback.
3. **DPI.** The shipped app is per-monitor DPI aware; the shell is not yet.
4. **Feature panels, one at a time against the mockup, starting with Tracks.**
   Tracks first because it is the panel the owner said he ignored, and the one
   Solo Piano exists to fix.

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
