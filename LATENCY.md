# Keyboard timing

Open **Measure latency** in the Log header of the Windows app. Pick Live keys,
MIDIConnect, or Autoplay. Closing the timing window removes the hook. Reopening
starts a fresh measurement session. Close and reopen when comparing devices or
settings. The existing injection backend and velocity protocol are unchanged.

The report uses note-ons from the last 512 MIDI messages per source. Note-offs
and sustain have their own records and do not increase the note-on denominator.
An ignored or already-held note-on can have zero injected events. Failed calls
and incomplete hook observations are shown explicitly; a missing observation is
never treated as zero latency. The rolling window can contain fewer than 512
note-ons because it also retains releases and sustain.

## What the numbers mean

| Value | Boundary |
|---|---|
| t0 | Timestamp supplied by `IMidiInput`; autoplay uses event dispatch entry |
| t1 | Immediately before the first injection call belonging to that message |
| t2 | Immediately after the final injection call belonging to that message |
| t3 | Latest keyboard-hook observation of its tagged events |
| Before first injection | t1 - t0, including instrumentation before the call |
| Time inside injection calls | Sum of individual call durations; excludes work between calls |
| Callback/dispatch to last keyboard hook | t3 - t0, only when every accepted event was observed and all calls succeeded |
| Last hook minus final call return | Signed t3 - t2; negative values are valid |
| Accepted events per note-on | Sum of accepted events from note-on handlers divided by sampled note-ons |

The hook runs before an event is posted to the target input queue. It can run
while the injection call is still in progress. These intervals overlap, so they
must not be drawn as an additive latency bar. The local tests observed t3 < t2.
See Microsoft's [keyboard hook documentation](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc).

The measurement ends at a hook, before the receiving game processes the key.
It does not measure sound onset, physical key travel, pre-callback MIDI transport
delay, or autoplay scheduling lateness. WinMM's t0 is at the `IMidiInput`
trampoline, after RtMidi has decoded the short message. Wooting supplies its poll
timestamp. The hook itself adds overhead. The receiving game may process many
events in a frame; event count alone does not establish a frame penalty.
[SendInput documents serial insertion](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput),
not the target game's processing rate.

## Implementation and memory ordering

`LatencyTelemetry.hpp` contains the header-only bounded ring and collector.
`InputLatency.cpp` owns the Windows hook and the thin injection wrapper.
`InputLatencyWindow.cpp` is the temporary Win32 viewer, independent of a future
ImGui shell.

- Producers include MIDI callbacks, autoplay workers, and the dedicated hook
  thread. The consumer is the UI or test thread. An SPSC queue would be unsafe.
- Each ring slot has a sequence number. A producer reserves a slot with a relaxed
  compare-and-exchange on the write index, writes its payload, and publishes the
  slot with a release store. The consumer's acquire load makes that complete
  payload visible. Its release store marks the slot reusable; the next writer's
  acquire load prevents reuse before the read finishes. The relaxed index CAS
  only assigns ownership; it does not publish payload data.
- A producer makes at most eight CAS attempts. Full queues or contention drop
  telemetry and increment a counter. They never drop MIDI or wait for the UI.
  Ring writes allocate no memory and acquire no locks. A temporarily descheduled
  producer can delay the consumer at the FIFO head; other producers still return
  promptly, dropping telemetry if necessary.
- A thread-local `Trace` groups all injection calls for one MIDI message.
  A process cookie and monotonic sequence in `dwExtraInfo` correlate events.
  The hook requires both that tag and `LLKHF_INJECTED`, ignores other keyboard
  input, and immediately forwards the event down the hook chain. It records no
  untagged physical typing. Sequence values are not reused on wrap.
- Tagging copies up to 64 keyboard events to a local stack buffer. Shared
  precomputed tables are never modified. Larger batches, non-keyboard input, and
  pre-existing extra-info values pass through unchanged and cannot produce a
  complete hook metric. No batch is split to accommodate measurement.
- The consumer joins observations and submission records in either order,
  expires incomplete joins after two seconds, and computes nearest-rank
  median/p95/p99 values. Its pending map is capped at 4096 entries. Allocation,
  sorting and text formatting happen only there.
- Hook installation, message pumping and teardown run on a dedicated thread.
  The UI can stall without starving that hook's message loop. Missing hook
  observations remain visible if Windows removes or bypasses the hook. Opening
  the viewer is opt-in; there is no hook thread or periodic telemetry polling
  while it is closed.

The wrapper preserves the inherited `NtUserSendInputCall`, including its batch
boundaries, to provide a baseline. It records failures, partial results, and
impossible counts such as the uninitialized stub's `69`. This does not repair
the backend itself. The existing engine still performs some cleanup injection
on the UI thread; moving that work is outside this measurement change.

## Build and verify

From PowerShell in the repository root:

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'MIDI++.sln' /p:Configuration=Release /p:Platform=x64 '/p:OutDir=D:\Dev\MIDIPlusPlus-modded\build\latency\app\' '/p:IntDir=D:\Dev\MIDIPlusPlus-modded\build\latency\obj\' /m
& .\tests\run-latency-tests.ps1
& .\tests\run-latency-tests.ps1 -ListPorts
& .\tests\run-latency-tests.ps1 -LoopbackPort 'MIDI'
& .\tests\run-latency-tests.ps1 -Legit
Push-Location .\build\latency\tests
.\LatencyTests.exe --ui-smoke
Pop-Location
```

Use an existing **loopMIDI** port for the loopback argument, not a hardware
synthesizer. On the tested machine its output is `MIDI`; RtMidi calls the input
`MIDI 0`. The fixture resolves the native input identity, confirms the loopback
route is receiving after an API change, then sends its measured sequence.

Test builds, copied configuration, raw QPC CSVs, and the timing-window screenshot
stay under `build/latency/`. The test installs a separate hook that swallows only
its own tagged events after the production measurement hook observes them, so
test notes do not type into the focused application. These are functional tests,
not a game latency benchmark. The tests disable the decorative splash but use
the actual PlaybackCore, MIDI2Key, MIDIConnect, WinRT, WinMM and injection code.

Verified locally on 2026-09-04:

- Release x64 app and test executable compile.
- Ring saturation, wraparound, 120000 concurrent write attempts, ordered payloads
  and exact accepted/dropped accounting pass.
- Out-of-order hook joins, signed timing, expiry, percentiles, partial injection,
  impossible return counts and the disabled path pass.
- loopMIDI through WinRT and WinMM delivers a triad, note-on/off, zero-velocity
  note-off, a non-default sustain cutoff, and changed/repeated velocity buckets.
- Autoplay and MIDIConnect use distinct measurement populations. A plain live
  note uses 1 event; a changed velocity bucket uses 5 in both the live and the
  autoplay path, and a repeated bucket uses 1 in both. Autoplay used to cost 7
  because it tapped ALT through two three-event calls; it now sends the same
  four-event tap in one call that MIDI2Key does. MIDIConnect uses 10 events per
  message.
- The timing window displays real captured results, changes source, closes and
  reopens; its screenshot was inspected for clipping.

Still unverified: physical piano and Wooting playing, two simultaneous physical
inputs, and delivery or audible response in a target game. The prior assumption
that upstream issue #44 proves a specific ALT failure is too strong: the
[issue](https://github.com/Zephkek/MIDIPlusPlus/issues/44) has a shortcut-related
title and no description. The ALT path is a candidate cause to reproduce, not
a confirmed diagnosis from that issue.

## The direct syscall thunk, measured and removed

`InputInjector.cpp` used to build a thunk at run time: it read
`NtUserSendInput`'s syscall number out of win32u's prologue, wrote a syscall
stub into an RWX page, and called that instead of `SendInput`. HANDOFF.md
section 4 recorded that its speed benefit had never been measured.

Measured 2026-09-05 on this machine, comparing the thunk against `SendInput`
directly, alternating rounds so scheduling and clock drift landed on both.

On the four-input ALT velocity tap the note path actually sends, with a
low-level hook installed to swallow the test input, the median call cost was
about 118us for both. The difference across four runs was +1534, -659, +58 and
-18 ns. **It changes sign between runs**, so it is noise, not a saving.

With `cInputs` set to zero, which still pays the user-to-kernel transition and
the argument validation but skips the hook round trip that dominates the figures
above, the thunk cost 399.0, 417.0, 418.5 and 424.5 ns against `SendInput`'s
400.5, 418.0, 420.5 and 424.5. That is 0 to 2 ns out of roughly 410, and it is
the most favourable framing available to the thunk, because it removes all the
delivery work the two paths share.

Against a real injection near 118us, 2 ns is 0.0017%.

The thunk was removed. What it cost was not theoretical: any setup failure left
a default that returned 69 and injected nothing, so keystrokes silently stopped
reaching the game and only the traced path in `InputLatency` noticed, and the
process carried a page of hand-written syscall bytes for no gain. Injection now
goes through `SendInput` behind the `InjectInput` pointer, which stays so the
tests can substitute an in-process recorder.

Do not reintroduce it without a measurement that beats this one.
