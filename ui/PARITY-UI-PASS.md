# Panel batch, 2026-09-07

Worktree: `D:\Dev\mpp-panels`, branch `astra/shell-parity`, base `0be2c79`.
This batch is **incomplete**. Changes are uncommitted and confined to `ui/`.

## Implemented

- SOLO labels use the button's content rectangle, including in the final table column.
- Files sort by name, size or modification date in either direction. The worker
  owns the ordered list, so mini mode and Prev/Next share the order. Sort choice
  persists in the shell configuration.
- Raised-surface highlights follow the upper rounded corners.
- Regular and mini mode share device-pill typography, state-pill padding,
  strip rows and the four utility slots. Mini mode is now 640 logical pixels
  wide so these controls fit without changing their treatment.
- File refresh is an icon. Export is a menu containing only Copy as sheet.
- Status drawing clips to the interior below its separator, including Loading.
- Log is reachable from both status bars. It captures cout, cerr, clog and their
  wide variants, including the KS read failure on wcerr. History is bounded to
  256 KB. Clear Log and Copy Log are icon controls. This is C++ stream capture,
  not interception of third-party DLL output written directly to OS handles.
- Mouse Play starts a cancellable countdown, default 3 seconds, adjustable in
  Settings. Global Play/Pause is immediate, or cancels an armed countdown.
  Configured bindings and failed registrations are shown above the transport.
- Startup requires acknowledgment of the focused-window typing warning before
  output can start. A reminder remains in the strip. Stop cancels countdowns,
  stops autoplay and disables live input and MidiConnect.
- MidiConnect is wired through a worker-owned ConnectInput interface. The
  native host supplies the real MIDIConnect implementation. Switching between
  Midi2Key and MidiConnect closes the outgoing input first. Changing devices
  while MidiConnect is active preserves that route. No MIDI output was added.
- Legit Mode is available in Settings and survives file loads and restarts.
- Shuffle Play persists and selects another library file at song completion.
  Prev/Next wrap through the sorted library; stopped selection stays stopped,
  while a selection during playback resumes the next file.
- Window opacity, 40 to 100 percent, is in Settings and persists with the shell
  preferences. The native host applies it to the main window.

## Cross-seat work still required

1. Device grouping: MidiInputDevice currently supplies only a backend-specific
   opaque id, decorated name and backend. Requested from Claude: a stable
   device/port grouping identity and undecorated display name, with
   hardware/software/unknown classification where it can be established.
   Display-name matching cannot safely distinguish identical keyboards. The
   existing flat list and backend radios remain pending that seam.
2. OutRange: do not simply enable the current flag. PlaybackCore.cpp:1037 folds
   the press through transpose_note(), but release_key() at 1074 looks up the
   original note. A folded press therefore has no matching release. The live
   fold in MIDI2Key.cpp:292 and autoplay's transpose_note() also disagree.
   Requested a shared folding/release correction and engine regression tests.
   The shell OutRange switch is not implemented pending that correction.
3. New behavior checks: tests/ belongs to Claude. Asked whether this seat may
   add the checks to tests/ShellTests.cpp or Claude will add them. No answer
   received, so no test files were edited and no disposable harness was made.

ConnectInput is supplied by the native host, so the existing shell and render
test projects do not need MIDIConnect.cpp just to link ShellEngine. This
supersedes the earlier request to add it to both test projects. A test that
uses NativeConnectInput itself will need that source linked.

## Regression checks to add to ShellTests.cpp

- FileBefore and Scan/SortFiles: both directions, equal size/date tie-breaking,
  modification metadata, persisted sort, mini and Prev/Next sharing the list.
- CaptureShellLog: narrow and wide errors, partial output, Unicode, concurrent
  writers, retention bound, snapshot refresh while the worker sleeps, ClearLog,
  and restoration of all stream buffers on destruction.
- PlayCountdown: no note before expiry, output after expiry, second click and
  Stop cancellation, Load/Seek/Restart cancellation, stale generation rejected,
  zero delay, configured delay surviving restart. Capture every injected event.
- Require acknowledgment in the constructor, attempt Play, PlayCountdown,
  LiveOpen, LiveActive, MidiConnect and AutoVolumeCalibrate, and assert no output
  before AcknowledgeTyping. Normal output works after acknowledgment.
- Fake ConnectInput: Open/Activate/Close thread ownership, failed open, switching
  routes, device change retaining MidiConnect, Stop and shutdown closing it.
- Legit Mode persists and remains enabled through a fresh Load.
- Prev/Next wrap in both directions, selecting while stopped emits no notes,
  playback advances under shuffle without immediate self-repeat when another
  file exists, and Stop does not start another file.

Extend RenderTests with 125 percent, mini Live and Autoplay, settings, Log,
countdown, sort and export menus, and the startup warning. The current test
renders regular mode only, with Key Mapping obscuring much of Tracks, and
still uses different canvas heights by skin rather than Panels::DesiredSize.

## Verification state

- Baseline tests/run-shell-tests.ps1 passed before edits, including the layout
  and AutoVol mutation-defended regressions.
- The final tests/run-shell-tests.ps1 -Render run passed all existing behavior
  checks and all four skins at 100, 150 and 200 percent, returning to 100.
- The final Release/x64 build succeeded at
  `build/shell-parity-verify/MIDIShell.exe`. The usual `build/shell` executable
  was still open for inspection and could not be overwritten. It is an earlier
  build and does not contain the final log-placement and route-retention fixes.
- Native UI inspected on the owner's 125 percent display: startup warning,
  configured key labels, regular/mini pill and utility-slot consistency, mini
  Live and Autoplay, and real engine error text in Log.
- That inspection found Log could open beyond the mini window. Its initial
  position and size are now constrained; the correction still needs native
  inspection. Native input stopped after the interruption.
- No latency suite, physical keyboard performance, real-game MidiConnect,
  new countdown injection timing, shuffle playback or opacity interaction has
  been claimed as verified. New behavior checks are still required.

The velocity editor rewrite, MIDI output and conversion pipeline were not
started. Nothing in SHELL-GAPS.md was removed or closed as optional.
