# Start here

Entry point for whoever picks this up next, assistant or human. Written
2026-09-04 at `input-path-r5`. Updated 2026-09-05 after the first functional
ImGui panels and the personal GitHub fork were added.

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

**The UI migration is in progress.** The separate ImGui executable now has real
fonts, DPI scaling, a MIDI library, Tracks and basic autoplay. It shares
`MidiParser` and `PlaybackCore` with the original app through `ShellEngine`.
Live input, the velocity editor, key mapping, mini mode and full playback
controls have not been ported. The existing release executable is preserved.

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

It resolves config and preferences beside the executable, so any working
directory works. The build copies the default config only when none exists.
Fonts are embedded except for the system's Segoe UI. Font and software licenses
are copied beside the executable. Open a file, choose a folder, drop a MIDI, or
pass its path on the command line. Folder scans are not recursive.

```powershell
& .\tests\run-shell-tests.ps1 -Render
```

This checks synthetic MIDI parsing, track controls, actual engine dispatch with
an in-process injection recorder, and the real DX11 renderer. PNGs are written
to `build\render-tests\`. It requires no MIDI hardware and sends no keystrokes
to other applications.

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

Completed 2026-09-05:

- **Fonts:** `ui/Fonts.cpp` loads Segoe UI regular/semibold through an absolute
  Windows font path. IBM Plex Sans regular/medium/semibold is embedded as
  resources under its OFL license. `PushFont(font, designSize)` and the existing
  DX11 backend's dynamic textures work with the vendored ImGui 1.93 WIP.
  No downgrade or font installation is needed. The earlier blank-window
  attempt was not reproduced. Another useful trap: moving the cursor after
  the final item in a child can assert in ImGui; draw-list text avoids that.
- **DPI:** PerMonitorV2 manifest, initial client sizing, `WM_DPICHANGED`, and
  a single scale for custom geometry. Style resets from pristine metrics on
  every DPI/skin change; `FontScaleDpi` scales logical font sizes once.
  Native Classic/Modern switching was exercised. All four skins rendered at
  100/150/200% and back to 100% through DX11/WARP. Moving a window between
  physical monitors with different scaling remains unverified.
- **Tracks:** real file parsing, original track indices, names, instruments,
  channel numbers, note counts, piano icons, mute/solo, Solo Piano, Unmute All
  and a computed silent-track count. Note-free conductor tracks are hidden.
  Program changes are followed by channel across the file. Mixed-instrument
  tracks are identified conservatively; Solo Piano mutes the whole track.
- **File library:** file picker, folder picker, refresh, cached search,
  clipped lists, drag/drop and Unicode paths. Parsing is on a worker.
  Paths with CJK characters load correctly, but the selected Latin fonts do not
  cover those glyphs; filename display still needs a CJK fallback font.
- **Basic autoplay:** Play in 3s gives time to focus the game; Stop and F4
  (when registration succeeds) stop playback. Velocity and sustain are wired.
  The shell uses Linear Fine, 88 keys, and no heuristic drum removal so the
  visible track state controls playback. Sustain mode is changed while stopped.
  Play starts from the beginning. Pause, seek, speed and transpose are next.
- **Settings:** skin family, light/dark, auto Solo Piano on load, and credits.
  Skin and folder preferences are saved separately from the legacy config.
- **Engine corrections:** valid drive-rooted filenames are accepted by the
  parser without allowing traversal or alternate streams. Autoplay tracks own
  their pressed notes/pedal, so muting cannot discard their eventual release
  or let a muted part release another part's note at the same pitch.

`ShellEngine` owns the player and command queue. Construction, loading,
play/stop, key cleanup and destruction run on its worker, and scheduling keeps
the inherited playback threads. The legacy hotkey listener is disabled only in
this host. The original app keeps its default constructor behavior.

Verified: shell tests, the existing legit-mode suite, rendered DPI/skin cases,
native file loading, mute/solo and appearance switching, and the original solution build
to `build/legacy-check/`. New-shell delivery into a game has not been tested.
The native file picker opens; completing its modal dialog remains unverified
because the desktop automation tool could not target its controls reliably.

Next: extend the Playback card, then add the collapsed velocity panel and its
editor against the mockup. Keep Tracks visible. The velocity and key mapping
design decisions in `HANDOFF.md` still apply. Do not restore inert device
controls or the old mock Casio device label before wiring live input.

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

`origin` is now [K-Alexandru/MIDIPlusPlus](https://github.com/K-Alexandru/MIDIPlusPlus).
`upstream` is `Zephkek/MIDIPlusPlus`. `input-path-r5` tracks the branch on the
personal fork. The pre-handoff `d60a33a` commits were pushed first, and this
continuation is committed and pushed on the same branch. `main` stays at
`e37ba7e`. The owner explicitly authorized automatic pushes.

The untracked `x64/Release/midi/` folder contains the owner's local music. Do
not add it to commits. All generated test binaries, fixtures and PNGs are in
the ignored `build/` directory.
