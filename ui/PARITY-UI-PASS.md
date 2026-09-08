# Panel batch, 2026-09-08

Worktree: `D:\Dev\mpp-panels`, branch `astra/shell-parity`.
The initial UI batch was committed as `c254d0a` before merging
`input-path-r5` at `1de7a8c` through merge commit `15679c2`.
This report supersedes the incomplete report from September 7.

## Built

All eight items in the Owner's UI pass are implemented:

- SOLO is centred over its button's content rectangle, including the last column.
- Files sort by name, size or modification date, ascending or descending.
  The worker owns that order, shared by regular mode, mini mode and Prev/Next.
- Raised highlights follow the rounded upper corners.
- Both modes share device-pill typography, state-pill padding, strip rows and
  the same four utility slots. Mini mode is 640 logical pixels wide.
- File refresh uses the existing Refresh icon.
- Export opens a menu containing only Copy as sheet.
- Status text is clipped below the separator, including Loading.

Sequencing items 2 through 5 are implemented:

- Log is reachable from both status bars, with Clear Log and Copy Log icons.
  It captures narrow and wide C++ output/error streams, including KS read
  errors on wcerr. History is bounded to 256 KB and refreshes while the engine
  worker sleeps. It does not intercept third-party DLL output sent directly
  to operating-system handles. The log opens inside the current viewport,
  including mini mode.
- Actual configured transport bindings and registration failures appear beside
  playback. Mouse Play starts a cancellable countdown, default 3 seconds,
  configurable from 0 to 10. Global Play/Pause remains immediate and cancels
  an armed countdown. Stop, Load, Seek and Restart also cancel it.
- Devices group on MidiInputDevice::group. The selected row has a transport
  selector; changing it retains the device. Duplicate names on one transport
  fall back to separate opaque IDs, with distinct port labels and ID tooltips.
  No hardware/software classification is guessed. Native inspection showed
  four MIDI device groups plus the separate direct Wooting route.
- MidiConnect uses the real MIDIConnect implementation through a worker-owned
  ConnectInput interface. Route changes close the outgoing input first.
  Device changes retain the active MidiConnect route. Stop and shutdown close
  it and release its keys. This is the existing keyboard protocol, not MIDI output.
- OutRange is available in Settings for 61-key mode and persists. Switching it
  closes live callbacks and releases held autoplay/live keys before changing
  the folding flag, then reopens the selected live input with its prior state.
  88-key mode disables the fold while preserving the preference.
- Legit Mode is available in Settings and remains applied after Load and restart.
- Shuffle Play persists and chooses another library file at song completion.
  Prev/Next wrap in the sorted list; stopped selection stays stopped and a
  selection during playback resumes. Stop invalidates a queued shuffle advance.
- Window opacity, 40 to 100 percent, is available in Settings and persists.
- A startup warning explains focused-window typing and Ctrl shortcuts before
  output is enabled. A reminder stays visible in both modes. Acknowledgment
  gates autoplay, live input, MidiConnect and AutoVol calibration.

## Persistent verification

Behaviour checks live in `tests/ShellTests.cpp`. InjectInput is captured before
any player is constructed. NativeConnectInput is tested with the substituted
MIDI factory, including its actual numpad protocol and worker-thread ownership.
No behaviour or render test types into the desktop.

Coverage includes grouping and duplicate names; log Unicode, partial messages,
concurrent writers, retention, Clear Log and idle snapshot refresh; actual
held-note release during OutRange changes in autoplay and live input; countdown
expiry, early-output exclusion and cancellation; warning gates; sorting,
Prev/Next, real Legit Mode behavior after Load, shuffle and persistence; and
MidiConnect route changes, failed opens, Stop and destruction.

`tests/run-shell-parity-mutations.ps1` is checked in. All nine deliberate
regressions were killed by their expected assertion, followed by restored
baseline passes:

1. Omit the autoplay release during an OutRange switch.
2. Omit the live mapped-key release during an OutRange switch.
3. Merge duplicate device names despite duplicate backend rows.
4. Stop capturing wide stderr.
5. Start typing before the countdown expires.
6. Bypass the startup acknowledgment gate.
7. Reset the real Legit Mode flag on Load.
8. Leave MidiConnect running after Stop.
9. Accept a queued shuffle advance after Stop.

The runner restores original source bytes even on failure and rebuilds the
baseline. Detailed logs are generated in `build/parity-mutations`.

`tests/run-shell-tests.ps1 -Render` runs the complete behaviour suite, including
0248ff0/dea0965 regressions, and the actual DX11 renderer. Render scenarios cover
all four skins at 100, 125, 150 and 200 percent, then return to 100 percent.
They include regular mode, both mini modes, Log, Settings, sort and export
menus, full and mini countdowns, the startup warning and mini Log. The harness
asserts popup isolation and font scaling. PNGs go to `build/render-tests`.

Native inspection at the owner's 125 percent display covered the startup
warning, visible bindings, Settings through the device pill, grouped device
names, regular/mini strip consistency, mini Live and Autoplay, log placement,
real engine errors, Clear Log, and opacity changing to 78 percent and back to
100 percent. Full layouts were also inspected across every skin and DPI.

Final Release/x64 output is `build/shell/MIDIShell.exe`.
The earlier native inspection used `build/shell-parity-verify/MIDIShell.exe`;
the final build also includes the transport selector's matching drawn chevron.

## Checklist corrections and remaining boundaries

SHELL-GAPS.md's claim that the shell pins 88-key mode is stale since 0248ff0.
The OutRange defect was a guaranteed unmatched release, not a possible one;
Claude's sounding_note correction is merged, and this batch supplies the
release-first toggle that the engine correction alone could not provide.
The old claims that Legit Mode is forced off and the sequencing 2 through 5
controls are absent are superseded by this implementation.

The duplicate AutoVol call in the original window remains the engine seat's
item; this batch does not change it. Drum detection, auto-transpose, the
settable velocity key and other later checklist obligations remain open.
The velocity editor rewrite, MIDI output and conversion pipeline were not
started. No item was removed as optional.

Physical MIDI-to-game performance, real-game MidiConnect, acoustic calibration,
mixed-monitor moves and the desktop-taking latency suite were not verified by
this batch. The latency suite was not run.
