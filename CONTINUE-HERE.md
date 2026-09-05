# Start here

Entry point for whoever picks this up next, assistant or human. Written
2026-09-04 at `input-path-r5`. Updated 2026-09-05 after the Playback card and
separate key mapping window were committed and pushed as `738052a`.

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
fonts, DPI scaling, a MIDI library, Tracks, playback controls and key mapping. It shares
`MidiParser` and `PlaybackCore` with the original app through `ShellEngine`.
Live input, the velocity editor and mini mode have not been ported in this
continuation. Live input and configured global hotkeys are owned by a concurrent
agent. The existing release executable is preserved.

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
- **Autoplay foundation:** Velocity and sustain are wired.
  The shell uses Linear Fine, 88 keys, and no heuristic drum removal so the
  visible track state controls playback. Sustain mode is changed while stopped.
  The Playback follow-up below replaces the first transport implementation.
- **Settings:** skin family, light/dark, auto Solo Piano on load, and credits.
  Skin and folder preferences are saved separately from the legacy config.
- **Engine corrections:** valid drive-rooted filenames are accepted by the
  parser without allowing traversal or alternate streams. Autoplay tracks own
  their pressed notes/pedal, so muting cannot discard their eventual release
  or let a muted part release another part's note at the same pitch.

Completed 2026-09-05, Playback and key mapping follow-up (`738052a`):

- **Playback:** one icon-and-label Play/Pause button, Restart, back/forward 10
  seconds, draggable seek, elapsed/total time, speed and transpose. The countdown
  and its state are deleted, along with all explanatory copy in the card.
  Play resumes the retained position, or starts at zero after reaching the end.
  Restart and seek preserve the current playing/paused state; reaching the end
  stops. Seek previews while dragging and commits on release. Speed spans
  0.25x to 2.00x in 0.05 steps; transpose spans -12 to +12 semitones.
- **Engine commands:** `Pause`, `TogglePlayPause`, `Restart`, `Back10`,
  `Forward10`, `Seek`, `Speed`, `Transpose` and `Remap` extend the existing queue.
  Numeric commands use `Command::amount`. Score commands require the current
  snapshot generation, including commands sent by the global-hotkey host.
  `Stop` remains generation-independent and resets the position to zero.
  Timing and mapping changes first join dispatch on the worker and release keys.
  Event timestamps are rescaled from original score times so the inherited
  scheduler's wall-time waits also work above 1x. Injection, cleanup and player
  destruction remain off the message-loop thread.
- **Key mapping:** a separate native ImGui platform window, 840 logical pixels
  wide, opened from the keyboard icon. It defaults open and persists visibility
  in `shell-settings.json`. Its piano uses ivory/ebony in every skin, starts at
  C2-C6, pages by octaves within A0-C8, and can show all 88 keys. Black keys win
  hit testing over the whites beneath. Clicking a key pauses autoplay and arms
  assignment; Escape or leaving the window cancels capture. Letters, numbers,
  shifted numbers and Ctrl combinations are supported by the existing injector.
  Unsupported characters report an error without changing the saved binding.
- **Mapping persistence:** the worker saves the selected `KEY_MAPPINGS.FULL`
  binding to the shell's config through a temporary-file replacement, preserving
  other config fields. The snapshot publishes the saved map and revision. These
  bindings are applied to autoplay. Transpose selects the shifted pitch's binding;
  pitches shifted outside A0-C8 have no output. Live-input map refresh belongs to
  the concurrent live-input work and was not implemented or verified here.
- **Rendered spec:** opened `skin-system.html` in Edge through Playwright and
  exercised Play/Pause, Restart, +/-10, seek, speed, transpose, paging, Full 88,
  remapping and window visibility. Measured 28/32px controls, a 6px seek track,
  a centered 56px speed value, a 160px transpose track, and key-window sizes of
  840 x 246.94px Classic and 840 x 268.39px Modern. The two ported panels convert
  CSS em sizing to the fonts' ascent/descent metrics locally. Other panels were
  not restyled. The mockup's inert previous/next-file buttons were not added.

Verified for this follow-up: the required Release x64 shell build and
`tests/run-shell-tests.ps1 -Render` pass. All four skins' PNGs were inspected
against the mockup at 100/150/200%, including the return to 100%. Additional
temporary harnesses under ignored `build/port-qa/` captured pause/resume,
fractional and bounded seek, restart, +/-10, running speed/seek, 2x dispatch
timing, transposed/remapped output, mapping persistence, invalid keys and stale
commands. Every captured injection ran off the calling thread. An ImGui input
harness exercised mouse seek, black-key remapping, paging, Full 88, Escape and
close/reopen across all 12 skin/DPI combinations. The native host created a
separate 840px Key Mapping window, saved its closed state and restored that
state on a second launch. The harnesses, logs and comparison sheets are local
verification artifacts, not changes to the concurrently owned `tests/` tree.

Still unverified: delivery from this shell into a game, physical mixed-DPI
monitor moves, and integration with the concurrent live-input/global-hotkey
work. No files under `MIDI++/` or `tests/`, hotkey registration code, or
`ui/Hotkeys.*` / `ui/LiveInput.*` were edited in this follow-up.

`ShellEngine` owns the player and command queue. Construction, loading,
play/stop, key cleanup and destruction run on its worker, and scheduling keeps
the inherited playback threads. The legacy hotkey listener is disabled only in
this host. The original app keeps its default constructor behavior.

Verified: shell tests, the existing legit-mode suite, rendered DPI/skin cases,
native file loading, mute/solo and appearance switching, and the original solution build
to `build/legacy-check/`. New-shell delivery into a game has not been tested.
The native file picker opens; completing its modal dialog remains unverified
because the desktop automation tool could not target its controls reliably.

Next: add the collapsed velocity panel and its editor against the mockup, then
mini mode. Keep Tracks visible. Coordinate the new queue commands and saved
mapping snapshot with the agent handling live input and global hotkeys. The
velocity design decisions in `HANDOFF.md` still apply. Do not restore inert device
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
