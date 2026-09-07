# Shell parity item 1

Verified 2026-09-07 on `astra/shell-parity`, incorporating `input-path-r5`
through `f789329`. Layout work was committed as `0248ff0` before AutoVol work.
Implementation changes are confined to `ui/`.

## 61-key and 88-key layouts

The visible Keys pill switches between the two layouts. The choice persists
as `SHELL_88_KEYS`; each layout retains its own FULL or LIMITED bindings.
The mapping editor and both footer labels describe the selected layout.
Switching pauses autoplay, drains live callbacks and releases outgoing keys
before rebuilding mappings. An active live input is reopened and reactivated.

The Wooting blocker was in the engine's selection of FULL versus LIMITED.
`f789329` resolves that selection; no second implementation was added here.

## AutoVol interaction

The main status strip has an AutoVol pill. Settings also exposes AutoVol,
including in mini mode. Its separate window shows the current state and lets
the user select the game window, refresh that list, calibrate or turn it off.

Calibration is an explicit action with a three-second cancellable countdown.
The dialog explains that it will focus the selected window and send 50
volume-down presses followed by the configured volume-up sweep. Playback and
live input stop first and must be resumed manually. The shell checks the
selected window's identity and confirms foreground ownership before calling
`toggle_volume_adjustment()` once on its worker. Failed focus leaves AutoVol
off. Closing the dialog or issuing another transport or mapping command
cancels a pending countdown.

AutoVol starts off each session. Loading another file disables it and marks
calibration needed instead of unexpectedly focusing the game and sending
keys. The user can recalibrate, explicitly turn it off, or continue playing
without volume adjustment. Changing game volume manually also requires
recalibration, as stated in the dialog.

The gap inventory's original description needed two corrections, now also
recorded in `SHELL-GAPS.md`: calibration itself does not focus a window, and
the original host calibrates twice because the enable toggle already does it.
The shell supplies the focus step and calls only the toggle. The live and
autoplay adjustment paths already existed in the engine.

## Verification

- Release/x64 `ui/MIDIShell.vcxproj` build passed.
- `tests/run-shell-tests.ps1 -Render` passed, including captured injection,
  Wooting layout mapping, parsing, transport, persistence and render checks.
  Final log: `build/parity-qa/final-suite.log`.
- A local captured-input layout harness passed selected-map dispatch, held
  key release, transpose, separate remap persistence, restart persistence and
  worker ownership. Source: `build/parity-qa/LayoutChecks.cpp`.
- A local AutoVol harness passed countdown, cancellation, exactly one sweep,
  failed focus, foreground timeout, reload invalidation, autoplay adjustment
  and worker ownership. A simulated MIDI2Key callback verified live velocity
  adjustment while enabled and no volume presses after disabling it.
  Source: `build/parity-qa/AutoVolumeChecks.cpp`. `AutoVolumeHost` is the UI-owned
  window-operation seam used to avoid desktop input in this harness.
- Native interaction at the actual 125% display scale verified layout changes
  and footer updates, selection persistence, the main AutoVol flow and the
  mini Settings entry point. Calibration focused a disposable receiver and
  delivered exactly 50 Left and 9 Right presses with the default configuration.
  Receiver source: `build/parity-qa/VolumeTarget.cs`.
- The AutoVol dialog was rendered and visually inspected in all four skins at
  100%, 125%, 150% and 200%, with a return to 100%. Captures and contact sheets
  are under `build/parity-qa/render/`. These local harnesses and artifacts are
  ignored build outputs; no files under `tests/` were changed.
- `git diff --check` passed. The latency suite was not run.

These checks establish mapping, focus and key delivery. They do not establish
a particular game's acoustic response, physical Wooting performance, or
moving detached windows between monitors with different DPI settings.

Items 2 through 6 in `SHELL-GAPS.md` remain queued. Nothing in those items is
closed by this change.
