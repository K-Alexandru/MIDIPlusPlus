# Context for an assistant without this machine

Current 2026-09-05. Read `CONTINUE-HERE.md` in the repository when machine
access is available. The earlier shell-only task is complete.

## Project

Windows MIDI-to-QWERTY app forked from Zephkek/MIDIPlusPlus. C++20, MSVC,
GPLv3. The original app uses Win32/GDI+; its successor is a separate
Dear ImGui/DX11 executable so UI development can continue independently.

Repository: https://github.com/K-Alexandru/MIDIPlusPlus/tree/input-path-r5
Local checkout: `D:\Dev\MIDIPlusPlus-modded`

## Implemented

- WinRT, WinMM and Wooting analog behind `IMidiInput`, with opaque device IDs.
  The original WinRT input path was confirmed reaching a game on 2026-09-04.
- Callback-to-keyboard-hook timing instrumentation. These readings exclude
  transport time before the callback and game/audio processing after the hook.
- Dispatch-time legit mode. Mechanically tested, judged unconvincing by ear,
  and kept as a Config checkbox in the original UI.
- Four runtime skins, raised/recessed drawing helpers, real Segoe UI and
  embedded IBM Plex Sans, and per-monitor DPI handling in the ImGui shell.
- Real MIDI file loading, folder browsing/search, and Tracks with instrument
  names, note counts, mute/solo, Solo Piano and Unmute All.
- Basic autoplay through the inherited engine, owned by a command worker.
  Play has a three-second focus countdown; Stop and an available F4 stop it.
- Automated MIDI/dispatch tests and renders of all four skins at 100/150/200%.
  New-shell in-game playback and physical cross-monitor moves are unverified.

## Next work

Finish the Playback card, then port the collapsed velocity panel and editor
against `skin-system.html`. Live MIDI devices, key mapping and mini mode still
need porting. This is a continuing UI migration, not a finished replacement.

The velocity editor should show the player's incoming velocity histogram and
live input/output dot, Sensitivity and Contrast macro controls, thumbnail
presets, an advanced point editor and A/B comparison. Edit a smooth curve and
sample to 32 steps. Linear Fine is the default; the custom Pro curve is the
recommended preset. Key mapping belongs in a separate 840px window.

## Constraints

- Injection, including key cleanup, must never run on the message-loop thread.
  `ShellEngine` is the command boundary. Keep it for later controls.
- Keep Tracks visible. The velocity editor starts collapsed.
- Classic is 1090 x 635 design pixels; Modern is 1090 x 728. Strip: 81px.
- Utility icons are drawn geometry, never single letters or icon-font glyphs.
- No em dashes in UI copy or new documentation. Remove redundant explanations.
- Colour cannot be the only state signal. Accent is for selection/focus/fills.
- Do not invent behavior or show mock devices and timing numbers as real.
- ALT is essential to the velocity protocol because velocity and note keys
  overlap. Do not reopen that settled decision.
- Keep legit mode at dispatch time. Do not bake it into parsed MIDI.
- Preserve GPLv3 attribution and bundled font/library licenses.
- `origin` is the personal fork; `upstream` is the original author. The owner
  has explicitly authorized committing and automatically pushing changes.
- The local untracked `x64/Release/midi/` library is not source. Do not publish it.
