# Start here

Entry point for whoever picks this up next, assistant or human. Written
2026-09-04 at `input-path-r5`. Updated 2026-09-06 after live velocity telemetry
and the final rendered spec comparison were completed.

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
  opaque id, never an index, and an id naming a device that is not present
  fails rather than opening a different one. Two at once is confirmed.
- Timing instrumentation exists and is honest about what it does not measure.
- Legit mode exists, works mechanically, and sounds wrong. Demoted to a Config
  checkbox. See the verdict in `LEGIT-MODE.md`.
- Autoplay and live input send the identical four-event ALT velocity tap, in
  the same injection call as the note it describes.

**The UI migration is in progress.** The separate ImGui executable now has real
fonts, DPI scaling, a MIDI library, Tracks, playback controls and key mapping. It shares
`MidiParser` and `PlaybackCore` with the original app through `ShellEngine`.
Configured global hotkeys and live input were merged from
`claude/global-hotkeys` through `cf9957b`. The velocity editor, mini mode,
input settings and status bar now use the real shell state. Incoming velocity
telemetry is carried through `EngineSnapshot`; the curve graph draws the
session histogram and the last played velocity. The existing release executable
is preserved.

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
Fonts are embedded except for the system's Segoe UI and CJK fallback fonts. Font and software licenses
are copied beside the executable. Open a file, choose a folder, drop a MIDI, or
pass its path on the command line. Folder scans are not recursive.

```powershell
& .\tests\run-shell-tests.ps1 -Render
```

This checks synthetic MIDI parsing, track controls, actual engine dispatch with
an in-process injection recorder, and the real DX11 renderer. PNGs are written
to `build\render-tests\`. It requires no MIDI hardware and sends no keystrokes
to other applications.

```powershell
& .\tests\run-native-tests.ps1
```

This is Windows' side of the same app, which the harness above cannot reach
because it drives ImGui IO in process and never sends a window message:
`WM_GETMINMAXINFO`, `WM_SIZE` and `ResizeBuffers`, `WM_DPICHANGED`, `WM_CLOSE`,
and a physical click at a physical pixel landing on the control drawn there. It
drives the real cursor and fronts a window, so it owns the desktop for about
half a minute; do not click anything while it runs. It works in
`build\native-tests\`, so it never touches the settings, config or MIDI folder
in use. `tests/NativeShell.ps1` is the reusable half, for one-off measurement
rather than only for this suite.

**Read the header of `tests/NativeShell.ps1` before writing any script that
clicks or screenshots this app.** This machine runs at 125% and MIDI++ is
per-monitor DPI aware; PowerShell is not. Without
`SetProcessDpiAwarenessContext`, every coordinate is off by 25% and the app
looks like it is ignoring input. `GetDeviceCaps(LOGPIXELSX)` reads 96 from a
virtualised process, so nothing warns you.

## ImGui UI progress

The shell and the main panels are built. The dated entries below record each
port; the latest entry supersedes earlier implementation-status statements.

`skin-system.html` is the spec. It is an operable mockup, not a picture: open it
and press things. Its measurements are real and were taken from the rendered
page, so build against them rather than guessing. `docs/design/README.md` is the
way in: committed screenshots of every skin and window mode, a loopback server
for the live page, and where in the source the exact geometry lives.

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

Completed 2026-09-05, velocity, mini and settings follow-up (`4d11a3f`):

- **Merge:** fast-forwarded `claude/global-hotkeys` to `cf9957b` before the
  implementation. Its configured hotkey block and live-input switch cases are
  unchanged by this port. No files under `MIDI++/` or `tests/` were edited.
- **Velocity:** collapsed by default, with the active curve dropdown and
  sustain cutoff in its header. Expanded view has preset thumbnails, a smooth
  response, dashed linear reference, the actual quantized output, Sensitivity,
  Contrast, A/B, an Advanced 32-sample editor, and inline new/duplicate/rename.
  Slider drags preview locally and commit on release. Tracks remains visible;
  the editor scrolls when the available height cannot fit it. The host requests
  1090 x 635/728 collapsed and 1090 x 1009/1115 expanded, bounded by the monitor
  work area. Advanced and naming consume more editor space without hiding Tracks.
- **Curve semantics:** the engine's 32 config values are input thresholds for
  output keys, not 32 output heights. `ui/VelocityModel.hpp` converts smooth
  output samples back to those thresholds. Built-in shapes come from the real
  player's `getVelocityKey()` results; unedited presets preserve all 127 input
  mappings exactly. The graph labels output as a game step rather than claiming
  to know a game's acoustic response. Only configured custom presets are shown;
  no Pro values were invented for configs that do not contain them.
- **Curve persistence:** custom names and threshold arrays stay under
  `CUSTOM_VELOCITY_CURVES`. `SHELL_VELOCITY` saves the selected preset, macros,
  optional manual samples, and sustain cutoff. Temporary-file replacement
  preserves other config fields. Linear Fine is the first-run default, and
  loading another score retains the selected response. A/B keeps an independent
  previous preset and edit, including across renames; audition state is temporary.
  A save failure leaves the applied response unchanged and reports an error.
- **Worker boundary:** curve application pauses and joins autoplay, updates the
  mapping, and resumes its retained position when appropriate. If a live port is
  open, committing an edit closes and reopens it to rebuild MIDI2Key's cached
  velocity lookup, releasing its mapped keys before replacing its state. This
  is a brief reconnect per committed edit, not a seamless live lookup swap.
  Construction, curve application, key release and destruction stay on the
  worker. The inherited live-input implementation itself remains unchanged.
- **Mini:** a 480px window with Live/Autoplay selection and a full-window icon.
  Live has curve selection and transpose. Autoplay has library selection, a
  file picker, Solo Piano, seek, Play/Pause, Restart and +/-10 seconds. Theme,
  Midi2Key, velocity, sustain and current status remain accessible. Key mapping
  is hidden in mini and retains its visibility preference for the full window.
  This port retains the snapshot's 88-key layout and two-state sustain control.
- **Settings:** device selection, rescan, activation and channel use the existing
  `LiveScan`, `LiveOpen`, `LiveActive` and `LiveChannel` commands. Every open
  carries the supplied opaque device id. Backend rows select available inputs
  from that same snapshot; the scan still prefers WinRT and exposes WinMM only
  as its fallback. It does not provide an independent forced-backend enumeration.
  Opt-in keyboard timing uses the existing collector and hook, with median,
  p95/p99, preparation/call timings and observation/failure counts. Closing
  Settings stops measurement. The caption states its callback-to-hook boundary,
  after transport and before the game; no end-to-end or transport-cost claim.
- **Status:** active curve, backend, playback/live state, silent-track count,
  and errors are computed from the snapshot. The stop-hotkey registration flag
  now reports when unavailable. **Fonts:** Windows CJK sources merge into all
  Classic/Modern font weights. Shared source bytes and ImGui's dynamic glyph
  loading preserve DPI changes and the existing Latin faces without installing
  or redistributing system fonts.

Verified: the required Release x64 shell build and
`tests/run-shell-tests.ps1 -Render` pass. Additional local harnesses in ignored
`build/shell-panels-qa/` verify all five built-ins over 127 velocities, macros,
A/B, advanced edits, duplication, renaming, config retention and failed writes.
Captured autoplay ALT taps match the editor's predicted output, with injection
and cleanup off the calling thread. ImGui input exercises macro clicks, A/B,
Advanced steps, inline naming and mini switches for every skin at 100/150/200%
and back to 100%. DX11 captures cover collapsed, expanded, advanced, constrained
height, both mini modes and Settings. CJK glyph lookup and rendered Chinese,
Japanese and Korean filenames pass. Local PNGs and logs are in that directory;
the required renderer's PNGs remain in `build/render-tests/`.

Remaining limits for this follow-up:

- **Transport comparison and buffers:** no Kernel Streaming backend exists,
  and the timing collector starts after transport. Settings states that Kernel
  Streaming is unavailable and supplies no fictitious buffer controls or figures.
- **Browser comparison:** the repository Edge capture and an in-app browser
  session now cover the rendered mockup. Settings, backend choice, Play,
  file filtering and mini transpose were exercised with no console errors.
- **Hardware/native host:** narrowed 2026-09-06 by `tests/run-native-tests.ps1`,
  which does drive native window automation. Now covered: the enforced minimum
  size, resizing and the swap chain rebuild, a physical click reaching the
  control drawn there at the display's real scale, and a clean `WM_CLOSE`.
  Still unverified: live curve reconnection under physical playing, and game
  delivery, both of which need hands on a keyboard. Mini-mode resizing is not
  covered because reaching it needs a click on a strip icon whose position moves
  with the panel work. The mixed-DPI move is written and skips itself on a
  single-monitor machine, which is what this one is.

Completed 2026-09-05, mockup reference (`docs/design/`):

- **Unblocked the browser comparison.** The rejection above was of the `file:`
  URL, not of the mockup. `tools/serve-mockup.ps1` serves the repository root
  read-only on `http://127.0.0.1:8756`, which browser tooling accepts. It is a
  raw `TcpListener`: `HttpListener` wants a URL ACL reservation and fails with
  access denied unelevated, and there is no Python or Node on this machine.
- **Deep links.** `skin-system.html` now reads `?skin=`, `?mode=`, `?keys=`,
  `?frame=` and `?only=`, so a case can be opened rather than described.
  Unknown values are ignored. `frame=1` hides the page prose and matches the
  document theme to the skin, which is what the capture script screenshots.
  Both additions are inert without the parameters; nothing the spec already
  showed has changed.
- **Committed screenshots.** `tools/capture-mockup.ps1` renders all four skins,
  both mini modes and both key mapping windows to `docs/design/` through
  headless Edge, 540 KB for ten PNGs. Regenerate after editing the mockup.
  `docs/design/README.md` is the index, and says which of the three routes into
  the spec answers which kind of question.

The screenshots are of the spec. `build/render-tests/` holds the equivalent
PNGs of the real shell, and comparing the two folders is the comparison the
entry above could not make.


Completed 2026-09-05, velocity telemetry (`MIDI++/VelocityTelemetry.hpp`):

- **The graph's missing half now exists.** The curve editor is specified to
  draw a histogram of the velocities you play and a dot for the note sounding
  now, and shipped as a pointer preview because nothing in the input path kept
  the velocities it saw. `velocity_telemetry` keeps them: 32 buckets, one per
  curve sample, plus the last velocity and a revision a reader can compare
  against to skip a redraw.
- **Cost on the callback thread** is three relaxed atomics and one release, no
  allocation and no lock. `record` sits after MIDI2Key's channel filter, so the
  histogram describes the part being played, and before the enable checks, so
  it still describes what was played when velocity output is switched off.
- **Header only and free of project references,** so consuming it needs no
  change to either vcxproj and no new link dependency.
- **Live input only.** Autoplay velocities come out of the curve under
  inspection, so feeding them back would draw the graph its own output.
- **A snapshot is not one instant.** Buckets are read one at a time, so a
  snapshot taken mid-performance can be a note or two out of step with itself.
  A histogram does not need better, and a lock on the callback thread to get it
  would be the wrong trade. Said here because the number must not later be
  presented as exact.

Verified: `tests/run-shell-tests.ps1` covers the decode (note on, velocity-zero
note off, real note off, control change, short buffer, null), the ends of the
velocity range against the ends of the bucket array, the refusal of 128, that
every bucket is reachable, and four threads recording 80,000 notes against a
concurrent reader with nothing lost. The single call inside
`MIDI2Key::ProcessMidiMessage` is not covered by a test: there is no fake
backend seam to feed an `IMidiInput` from, so the decode was moved into
`observe` where it can be driven directly, leaving that line with nothing to
get wrong.

Completed 2026-09-06, velocity telemetry consumer and final rendered comparison:

- **The engine snapshot carries live playing data.** `EngineSnapshot` includes
  `playedVelocities`. Normal worker publications copy the producer snapshot,
  and `ShellEngine::Snapshot()` refreshes it by revision while the worker is
  sleeping, so live playing can update the graph while autoplay is stopped.
- **The graph is live.** The 32 buckets are normalised to the largest bucket and
  drawn behind the curve. A zero total draws no bars. A non-zero `last` draws a
  labelled accent dot on the smooth response curve. The old pointer preview
  caption and hover dot are gone. Panel geometry is cached by telemetry revision.
- **Reset semantics are session scoped.** Loading another file does not reset
  the histogram. The mockup labels it as the user's playing this session, and
  the producer records live input independently of the selected autoplay file.
- **Rendered spec reconciliation is complete.** All full, expanded velocity,
  advanced velocity, mini, Settings and key-mapping states were compared across
  Classic, Classic Dark, Modern and Modern Dark. The native shell already held
  the earlier panel fixes. The mockup was corrected to show the real file tools,
  mini pills and transpose controls, and the real WinRT, WinMM and Wooting input
  choices without invented Kernel Streaming figures or buffers.
- **Verification passed.** Release x64, `tests/run-shell-tests.ps1 -Render`, the
  ignored telemetry panel harness across seven modes and four skins at
  100/150/200 percent and back to 100, and the refreshed key-mapping harness at
  100/150/200 percent all passed. A synthetic live distribution confirmed 59
  notes, last velocity 96, histogram rendering and the live dot. Zero-telemetry
  runs confirmed that no histogram or dot is drawn.

The work stayed inside `ui/`, `skin-system.html`, `docs/design/` and this handoff.
Nothing under `MIDI++/` or `tests/` was changed. Physical live MIDI, real game
delivery from the new shell, native window resizing and physical mixed-DPI
monitor moves remain unverified.

Completed 2026-09-05, first Wooting session:

- **A Wooting played the wrong notes**, and the cause was the analog backend's
  own scancode-to-note table having the virtual-piano layout upside down: the
  bottom letter row first, the number row last. The shipped `KEY_MAPPINGS.FULL`
  is the other way round, so "1" sounded A5 rather than C2 and a run of 1 2 3
  crossed an octave between 2 and 3.
- **`SetWootingScancodeNoteMap` was dead API.** Nothing called it, so the
  invented table was the only thing a Wooting could play.
  `WootingScancodeNoteMapFrom` now builds the map from the user's own
  `KEY_MAPPINGS.FULL`, and `MIDI2Key::OpenDevice` feeds it in whenever a
  Wooting id is opened, so a remap is picked up on the next open.
- **Black keys stay unmapped, deliberately.** Their bindings are shifted
  characters, and a shifted key is two physical keys to the analog SDK, so
  there is no one scancode to attach the note to. A Wooting therefore plays the
  36 unshifted white keys, C2 to C7, and nothing else. That is a real limit,
  not an oversight, and it is the next thing to solve if analog playing matters.
  Solved by the shift amount in the 2026-09-06 entry below, which is how the
  upstream app reaches them too.

Still unverified: this was fixed from the code and the reported symptom, and
the tests cover the table, but no Wooting was in the loop here. The remaining
suspect if notes are still wrong is the SDK's own keycode mode: the backend
asks for Set 1 scancodes and trusts what comes back.

Closed 2026-09-06, the Roblox clip toast:

- **It stopped, and nothing on our side changed.** The "Saving clip" toast that
  prompted `tools/record-keys.ps1` no longer fires with either build, a day
  later, with the same config and the same passages. The cause was never
  identified, so treat the three culprits ruled out in `cebfa28` as still ruled
  out and start from the recorder if it comes back.
- **What was established while it lasted:** Roblox binds Alt+1 to screenshot and
  Alt+C to clip, and those are the first and last of the 32 velocity keys
  `1234567890qwertyuiopasdfghjklzxc`. Only the experience developer can switch
  the shortcuts off, with
  `game.StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.Captures, false)`.
  There is no player-side setting.
- **Not a reason to reserve velocity keys.** Reserving `1` and `c` was proposed,
  vetoed, and reverted. Velocity stays on ALT and stays 32 levels.
- **The one real question it left open** is whether upstream's
  `NtUserSendInput` syscall thunk, removed in `e678893`, made our keystrokes
  more visible to other applications' shortcut handling than upstream's are.
  That was never measured either way. It is item 5 below.

Completed 2026-09-06, Wooting parity and the two lags:

- **The black-key limit was the missing shift amount, not a missing table.**
  wooting-analog-midi maps white keys only as well: its default layout runs
  N1-N0, Q-P, A-L, Z-M straight up the whites, exactly as ours does. Holding
  Left Shift adds Shift Amount semitones to every key, so with the owner's
  setting of 1 the shift is the sharp. Building a black-key scancode table
  would have been the wrong fix for a problem the upstream app solves with an
  offset.
- **The three controls now exist**, under `WOOTING_ANALOG` in the config, named
  and defaulted as upstream names and defaults them so a number carried over
  means the same thing: `TRIGGER_THRESHOLD` (its Note Trigger Threshold, 0.5),
  `SHIFT_AMOUNT` (its Shift Amount, 12) and `VELOCITY_SCALE` (its Velocity
  Scale, 5.0, applied as `rate * scale / 100`). `RELEASE_FRACTION` has no
  upstream counterpart: upstream releases at its single threshold and a key
  resting there stutters, so ours keeps the gap it always had and expresses it
  as a fraction of the trigger.
- **Two behaviour changes fall out of matching those defaults.** The trigger
  moves from 0.35 to 0.50 and the velocity scale from an implicit 4.0 to 5.0.
  Anyone who liked the old feel writes 0.35 and 4.0 into the config.
- **A held key remembers the note it sounded.** The backend tracked a bool per
  scancode and recomputed the note to release. With a shift that can be let go
  while the key is still down, that would have released a pitch nobody was
  holding and left the real one stuck down in the game. It now stores the note
  actually sent. Upstream does the same thing and for the same reason: a shift
  change does not retune a note that is already sounding.
- **A shift past the MIDI range plays nothing** rather than wrapping to a pitch
  nobody asked for.
- **Changing a keybind no longer waits on the disk.** Every remap reparsed the
  whole `config.json`, reserialised it, and replaced it with
  `MOVEFILE_WRITE_THROUGH`, which waits for the physical disk. Per keystroke.
  The parsed config is now held in memory, the binding applies and publishes at
  once, and the file settles 400 ms later through the same atomic rename
  without the write-through. A curve commit still writes immediately, because
  it happens on a slider release rather than per keystroke and its documented
  behaviour is that a failed save reports and leaves the applied response alone.
- **A config that failed to parse is never written back.** It is held as JSON
  null, and the earlier code would have rebuilt an object around a single
  binding and replaced the user's settings with it.
- **Superseded commands are dropped from the queue.** `Load`, `Seek`, `Speed`
  and `Transpose` all carry an absolute target, so when a newer one of the same
  kind is already queued the older cannot still matter. Clicking through a
  folder no longer parses every score passed on the way to the one wanted.

Not the cause, though it was the first guess: the 6.5MB `MIDIConnect` table is
not built on the first file click. `ensurePlayer` already runs at engine start,
so the `VirtualPianoPlayer` construction inside `Action::Load` is a defensive
branch that never fires. What is left of the click is the parse itself plus
`process_tracks`, which is real work on a worker with `busy` published and
"Loading..." already shown.

Verified: the shell Release build, the original solution build, the full
`tests/run-shell-tests.ps1` with and without `-Render` across all four skins at
100/150/200%, and the existing legit-mode suite. New coverage drives the
velocity formula and its scale directly, the settings round trip, the config
block's optional fields and every rejected range, and a live `ShellEngine`
proving three rapid remaps all reach the file, that unrelated config sections
survive, that no temporary file is left beside the config, and that a remap
still settling at shutdown is written on the way out.

Still unverified: no Wooting was in the loop, again. The poll loop's own shift
handling needs the hardware, so what is tested is the velocity conversion, the
settings and the config, not the loop that reads the SDK buffer. Nobody has
measured the remap latency before and after either; the write-through wait and
the per-keystroke reparse are gone by construction, but no number was taken.
There is no UI for the three settings: they are config-only, and adding one
means an `EngineSnapshot` field and `ShellEngine::Action` entries, which belong
to whoever owns the panel.

Completed 2026-09-06, two MIDI devices at once (item 4, now closed):

- **Confirmed, and it was not already right.** `269c2f2` replaced three
  disagreeing index spaces with opaque ids, which fixed what the combo and the
  opener disagreed about. It did not fix how a WinMM id resolves back to a port,
  and that had two faults that only appear with two devices present.
- **RtMidi welds the index into the name.** `MidiInWinMM::getPortName` appends
  `" <portNumber>"` to every name, so this machine reports `MIDI 0` and
  `loopMIDI Port 1`. The name half of an id was the half meant to outlive
  renumbering, and it carried the number it was supposed to outlive: unplug the
  first device, the second becomes `loopMIDI Port 0`, the stored name stops
  matching, and resolution falls back to the index that just moved. Names are
  now compared, and stored in ids, with that one trailing numeric token removed.
  A device genuinely called `Digital Piano 2` keeps its number, because RtMidi's
  suffix is always last and only one token is taken.
- **A missing device was substituted, silently.** The old resolution ended
  `if (target >= count) target = (index < count) ? index : 0;`, so asking for a
  keyboard that had been unplugged opened whatever was at port 0 instead. That
  is the opposite of what `MidiInput.hpp` promises, and it is worse than an
  error because an error is visible. `ResolveWinMMPort` returns -1 and the open
  fails.
- **The index is the tie-break, not the answer.** Two keyboards of the same
  model report the same name, which is what RtMidi's suffix was for. The
  recorded index is consulted first and accepted only when the name at it
  agrees; otherwise the first name match wins. RtMidi's number goes back onto a
  displayed name only where two rows would otherwise read identically.
- **WinRT was already correct and is untouched.** Its ids are device interface
  paths, `FromIdAsync` either returns the port or nothing, and there is no
  renumbering and no fallback. It is also the list the app actually uses:
  `EnumerateMidiInputs` only falls back to WinMM when WinRT reports nothing.

Verified: the shell Release build, the original solution build, the full
`tests/run-shell-tests.ps1` with and without `-Render`, and the legit-mode
suite. `PortResolutionTests` covers the suffix stripping, renumbering, absent
devices, identical names, stale indices, legacy ids with no name, WinRT and
Wooting ids offered to the WinMM resolver, a malformed index, a name containing
the separator, and every id this machine's own WinMM enumerator hands out
resolving back to its own distinct port. `TwoDeviceTests` opens both real
inputs at once and checks each landed on the port it was given and that closing
one leaves the other alone.

Still unverified: `TwoDeviceTests` ran against WinRT, because that is what
`EnumerateMidiInputs` returns on this machine, and it skips itself on a machine
with fewer than two inputs rather than failing. So the WinMM open path is
covered by the resolver's tests and not by a live two-port open. Both inputs
were loopMIDI ports; a real piano alongside a virtual port was not tried, and
nothing here sent or received an actual note through two devices at once.

Closed 2026-09-06, item 5, which was already done:

- **Both halves were finished and nobody updated the list.** `245479a`
  collapsed `MIDIConnect`'s `m_noteMapping[128][128][10]`, and `e678893`
  removed the `NtUserSendInput` thunk after measuring it at 0 to 2 ns. Item 5
  and `HANDOFF.md` section 4 both still described them as present, which is how
  a session gets spent rediscovering that there is nothing to do.
- **Verified rather than believed.** The evidence for the table was a comment
  saying it was gone. `sizeof(MIDIConnect)` is now asserted under 32KB in
  `MIDIConnect.cpp`, where the class is complete, so the member cannot grow
  back quietly. The old layout was 6.5MB and the object is about 21KB.
- **The stub is genuinely gone**, not merely unused: nothing under `MIDI++/`
  allocates an RWX page or reads a syscall number, only comments recording why,
  and the existing latency suite already asserts that injection routes through
  `SendInput` with no stub to initialise.

Still open from that section of `HANDOFF.md`, and untouched here: the ALT
velocity preamble is still four events where one might do, and removing it
needs the target game's protocol established first rather than guessed at.

Completed 2026-09-06, Wooting Settings panel:

- **The three upstream controls are no longer config-only.** Settings shows
  Note trigger threshold, Shift amount and Velocity scale only when the exact
  selected input is `wooting:analog` and its backend is Wooting analog. A MIDI
  port gets none of those controls or their layout space. `RELEASE_FRACTION`
  remains config-only and is preserved when any visible setting is saved.
- **Dragging is live and saving is settled.** `EngineSnapshot` carries the
  values and three `ShellEngine::Action` cases apply them on the worker to
  `midi::Config::getInstance().wooting`, the locked analog backend and the
  retained `configJson`. Drag previews are coalesced and call `touchConfig()`;
  release calls `flushConfig()` through the existing atomic rename. No second
  config parser or write-through path was added.
- **The ranges and upstream defaults are the UI contract.** Trigger is 0.01 to
  1.00 in 0.01 steps, shift is -127 to +127 semitones in whole steps, and
  velocity scale is 0.1 to 20.0 in 0.1 steps. Defaults are 0.50, 12 and 5.0.
  The shift row says to set it to 1 and hold Left Shift to play a black key.

Verified in software: the Release x64 shell build and
`tests/run-shell-tests.ps1 -Render` pass. An ignored engine harness covered
defaults, range clamps, backend preview, release-time file commit, unrelated
config retention, the untouched release fraction and restart persistence. An
ignored DX11 input harness found and dragged the real ImGui controls, switched
to a WinRT row and confirmed the Wooting controls disappeared, then rendered
and inspected Settings in all four skins at 100/150/200 percent and back to
100. The connected Wooting enumerated and opened during that harness.

Still unverified: no physical Wooting key was pressed, so trigger feel, the
poll loop's held-Left-Shift path, strike velocity under real playing and game
delivery remain hardware checks for the owner. Nothing under `MIDI++/` or
`tests/` was edited for this panel.

Completed 2026-09-06, the ALT preamble and half of item 6:

- **A note now travels in the same injection as its velocity.** A note whose
  bucket changed was two calls: the four-event ALT tap, then the note.
  `SendInput` inserts one call's events serially and puts nothing between them,
  and promises nothing across two, so whatever landed in that gap took the
  velocity the tap had just set. One call now, same events, same order.
- **MIDI2Key's tap buffer was a static member.** Two callback threads wrote the
  same four `INPUT`s and each could send the other's velocity. That is the bug
  `269c2f2` fixed in `MIDIConnect` by making its batch a local; MIDI2Key kept
  the shared one. It is a local now, which is also what makes the merge
  possible.
- **Live input sent its volume adjustment between the tap and the note**, up to
  forty arrow-key events wide. Autoplay already adjusted volume first, so live
  now matches it and nothing sits between a tap and its note.
- **The tap itself is unchanged, and that is the answer to item 5's leftover.**
  Four events is the least a modified keypress can be: ALT down and the key
  down are what the game listens for, and the two ups exist so the next note is
  not typed with ALT held. `HANDOFF.md` calls this four events where one would
  do. One needs the game to accept something that is not a modified keypress,
  which is the game's protocol and not ours, and establishing that needs
  someone playing rather than someone reading. Now said in the code as well.
- **MIDI to virtual piano sheet exists**, in `MIDI++/SheetExport.hpp`. Header
  only and free of project references, so it costs no vcxproj change. Notes
  inside a 45 ms window are one bracketed chord, gaps become spaces when a
  tempo is supplied and a single space when it is not, lines wrap, and notes
  the mapping does not carry are dropped and counted rather than guessed at.

The rest of item 6 is blocked on tools this machine does not have. `python.exe`
here is the Microsoft Store alias stub, and there is no Node, no ffmpeg and no
`yt-dlp`. The transcription half wants all of those plus a PyTorch sidecar, so
starting it is an install decision for the owner rather than something to do
quietly.

Verified: the shell Release build, the original solution build, the full
`tests/run-shell-tests.ps1` with and without `-Render` across all four skins at
100/150/200%, and the legit-mode suite. The batching test groups captured
injections by call and asserts that a call opening with ALT down carries more
than four events, which is exactly what the old two-call path could not satisfy.
The sheet tests cover chords, the window boundary either side, spacing with and
without a tempo, the silence cap, wrapping, unsorted input, unmapped notes and
an empty score.

Still unverified: nothing here was played into a game. The merge is a promise
`SendInput` documents rather than one measured on this machine, and whether the
game can accept fewer than four events is still unknown and still needs someone
at the keyboard. `SheetExport.hpp` has no caller: turning it into a button is
an `EngineSnapshot` field and an `Action`, which belong to the panel owner.

Completed 2026-09-06, the Wooting poll loop finally has tests:

- **It had none, and that was the largest untested thing in the tree.**
  Everything the analog backend decides lived inside a `while` loop that needs a
  keyboard on the desk, so it was written, shipped and reported as unverified
  three times without ever being exercised. `WootingPollStep` is that loop with
  the SDK and the clock taken out: the buffer, the note map, the settings and
  an elapsed time go in, note ons and note offs come out. It allocates nothing,
  because it still runs at 1kHz on its own thread.
- **The hazard it was hiding.** A key sounds `note + shift`, and the shift can
  be let go while the key is still held. Releasing what the map says now rather
  than what was actually sounded would send a note off for a pitch nobody was
  holding and leave the real one down in the game with nothing left to release
  it. The state carries the note that was sent, and there is now a test that
  fails if it stops.
- **Injection moved out from under the lock.** The loop used to call the
  callback, which runs the whole injection path, while holding the mutex that a
  settings change waits on. It now decides under the lock and sends outside it.
- **Closing the device is one more poll of an empty buffer**, rather than a
  second copy of the release logic. A key that is not in the buffer has been
  let go, which is already what the step does.

Twelve cases: the trigger and either side of the release gap, a key already
down not sounding twice, a key leaving the buffer, shift raising a note, shift
released mid-note releasing the note that sounded, shift arriving mid-note not
retuning it, a shift key too lightly held to count, a shift past either end of
the MIDI range staying silent and leaving nothing to release, the shift key and
unmapped keys sounding nothing, two keys released independently, and a faster
strike being louder than a slower one to the same depth.

Verified: the shell Release build, the original solution build, the full
`tests/run-shell-tests.ps1` with and without `-Render` across all four skins at
100/150/200%, and the legit-mode suite.

Still unverified: no Wooting was in the loop here either. What is tested is
every decision the loop makes; what is not is the SDK underneath it, whether
the keycode mode really returns Set 1 scancodes, and how any of it feels to
play. The panel owner reports the device enumerates and opens, which is further
than this has been before.

Completed 2026-09-06, Copy as sheet and the three panel follow-ups:

- **The sheet converter now has a caller.** Playback has a Copy as sheet
  action beside the loaded file. The engine collects note-on events from the
  tracks that are currently audible, so mute and solo affect the result, and
  uses the file's earliest explicit tempo for rhythmic spacing. A file without
  an explicit tempo stays on the converter's flat one-space fallback rather
  than receiving a guessed tempo.
- **Copying goes to the clipboard.** Virtual piano sheets are made to be pasted
  into another window, and a clipboard action does not create an unexpected
  sidecar beside the owner's MIDI file. The worker publishes the finished text
  through `EngineSnapshot`; the message-loop thread owns the Win32 clipboard.
  The result line reports characters written, shared chord notes merged, and
  unmapped notes dropped when those counts are nonzero. There is no generic
  Lossy line: it did not say anything useful about this particular export.
- **The exporter keeps three accounting buckets.** Claude's `14b0b5d` fix is
  included here as `cf95c30`: a chord that maps two notes to one character
  writes that character once and counts the other note as merged. Therefore
  `notes + merged + unmapped` equals every note handed to the converter without
  changing `notes` from its documented meaning, characters written.
- **Mini mode has Settings.** The device pill and right control cluster reserve
  the additional 40 pixels worked out in the panel spec. Full and mini modes
  call one Settings helper, including the popup and the latency collector
  cleanup, so closing the mini popup cannot leave a copied cleanup path behind.
  The mini popup is capped at 344 by 544 design pixels.
- **MUTE and SOLO no longer touch.** Each column has one spacing step around
  its control and its own centered header, while retaining the existing
  `##mute-heading` and `##solo-heading` IDs.

Verified: the Release x64 shell build and the complete
`tests/run-shell-tests.ps1 -Render` suite pass. All four skins rendered at
100/150/200% and returned to 100%, and all twelve captures were inspected. A
native DirectX 11 and ImGui harness clicked the real Copy as sheet control,
matched the Windows clipboard to the engine snapshot, and opened the shared
344 by 544 mini Settings popup. The owner's Beethoven file produced 5,370
written notes, 1 merged note and 0 unmapped notes from 5,371 note-ons in 2,771
groups, with bracketed chords. The Kaine file produced 1,581 written, 5 merged
and 0 unmapped from 1,586 note-ons in 941 groups, also with bracketed chords.
Muting their only note track produced an empty export, confirming that silent
tracks are excluded.

Still unverified: neither generated sheet was pasted into or played in a game,
and this was not a manual end-user walkthrough of the packaged window. The
owner's two available MIDI files had no unmapped notes, so the nonzero unmapped
status was covered by converter tests and code inspection rather than a real
file. Mini Settings was opened through the native interaction harness, but an
active latency measurement was not started and then closed through that popup.

Fixed 2026-09-06, the frame rate while using Key Mapping:

- **The app called itself backgrounded whenever you used its second window.**
  `ui/Shell.cpp` paced the loop with
  `MsgWaitForMultipleObjectsEx(..., GetForegroundWindow() == hwnd ? 16 : 80, ...)`.
  Viewports are enabled and Key Mapping is a separate top-level window, so that
  test is false for as long as the Key Mapping window has focus, which is
  exactly while you are remapping keys. It now asks whether the foreground
  window belongs to this process, which covers every window the app owns
  including any added later.
- **Measured, before and after, with the Key Mapping window focused:** 66.5 ms a
  frame and 15 fps before, 16.3 ms and 61 fps after. The main window focused was
  62 fps either way, which is why this never showed up in a screenshot.
- **What it is not.** Loading the largest file in the owner's folder, a 117 KB
  score, costs 22 ms end to end on the worker: 1.4 ms of parsing, 11 ms of
  `process_tracks`, the rest stop and publish. Clicking a file is not waiting on
  that work. The 80 ms idle pace beside a game is deliberate and unchanged.

Verified: the Release shell build and `tests/run-shell-tests.ps1 -Render` across
all four skins. The measurement used a temporary frame counter and a script that
finds the Key Mapping window by title and focuses it; neither is committed.

Still unverified: whether this is the whole of what the owner calls laggy. The
file list is inside the main window, which measured 62 fps throughout, so if
clicking a file still feels slow after this, the cause is somewhere the frame
rate and the load time have both now been ruled out of.

`ShellEngine` owns the player and command queue. Construction, loading,
play/stop, key cleanup and destruction run on its worker, and scheduling keeps
the inherited playback threads. The legacy hotkey listener is disabled only in
this host. The original app keeps its default constructor behavior.

Verified: shell tests, the existing legit-mode suite, rendered DPI/skin cases,
native file loading, mute/solo and appearance switching, and the original solution build
to `build/legacy-check/`. New-shell delivery into a game has not been tested.
The native file picker opens; completing its modal dialog remains unverified
because the desktop automation tool could not target its controls reliably.

Next: most of what is left needs the keyboard rather than the compiler. One session
with the Wooting settles trigger feel, held-Left-Shift black keys, strike
velocity and delivery into a game; a second device plus a piano settles playing
through two at once; and live curve reconnection and native window resizing are
the same kind of check. Copy as sheet is now reachable from Playback. The
transcription half of the YouTube pipeline still requires an owner-approved
tool installation. The rendered browser comparison is complete and reproducible with
`tools/serve-mockup.ps1`, `docs/design/` and `build/render-tests/`.
The velocity design decisions in `HANDOFF.md` still apply. Keep Tracks visible
and retain the real device ids and measurement boundaries.

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
- **Four events is the floor for the tap.** ALT down and the key down are the
  modified keypress the game listens for, and the two ups exist so the next
  note is not typed with ALT held. `HANDOFF.md` calls it four where one would
  do; one needs the game to accept something that is not a modified keypress,
  which is the game's protocol. What was actually costly was sending it as a
  separate injection call, and that is fixed. Do not go looking for a shorter
  tap without first establishing what the game accepts.
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
3. **Wooting analog playing.** The connected device now enumerates, opens, and
   has conditional Settings controls for trigger, shift amount and velocity
   scale, and as of 2026-09-06 every decision its poll loop makes is tested.
   Physical key travel, held-Left-Shift black keys, velocity feel and
   game delivery still need the owner to play it. The three remaining gaps
   against wooting-analog-midi stay out of scope by design: polyphonic
   aftertouch from continuous key depth, a second per-key note map, and a MIDI
   channel selector.
4. **Two MIDI devices at once.** Closed 2026-09-06; see the entry above. The
   ids from `269c2f2` were right and their WinMM resolution was not. What is
   left is playing through two devices for real, rather than opening two.
5. **Closed, and it was closed before this list said so.** Both halves were
   done and the list was never updated: `245479a` collapsed `MIDIConnect`'s
   table and `e678893` removed the syscall stub. Confirmed 2026-09-06 and now
   held by a `static_assert` and a test. See the entry above.
6. **YouTube to MIDI pipeline.** The sheet half is complete as of 2026-09-06:
   the tested converter in `MIDI++/SheetExport.hpp` is wired to Copy as sheet in
   Playback, includes only audible tracks, uses an explicit file tempo when one
   exists, and reports merged and unmapped notes. The transcription half is
   blocked on tools this machine does not have: `python.exe` is the Store alias
   stub, and there is no Node, ffmpeg or `yt-dlp`. It wants those plus a PyTorch
   sidecar, so it starts with an install decision rather than with code.

## Repository note

`origin` is now [K-Alexandru/MIDIPlusPlus](https://github.com/K-Alexandru/MIDIPlusPlus).
`upstream` is `Zephkek/MIDIPlusPlus`. `input-path-r5` tracks the branch on the
personal fork. The pre-handoff `d60a33a` commits were pushed first, and this
continuation is committed and pushed on the same branch. `main` stays at
`e37ba7e`. The owner explicitly authorized automatic pushes.

The untracked `x64/Release/midi/` folder contains the owner's local music. Do
not add it to commits. All generated test binaries, fixtures and PNGs are in
the ignored `build/` directory.
