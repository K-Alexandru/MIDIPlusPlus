# MIDI output: a route switch

Written 2026-09-07 for whichever seat builds this. Nothing below is
implemented. The decision that shaped it: **one output target for the whole
app**, not keystrokes and MIDI in parallel. Live input and autoplay both send
MIDI to a chosen output port *instead of* injecting keystrokes.

Read `HANDOFF.md` sections 3 and 4 first. `MidiInput.hpp` is the model this
copies; read it before writing `MidiOutput.hpp`, because the two should be
recognisably the same interface and the input side already paid for the
mistakes.

## Why it was asked for

Reported 2026-09-06: *"is there a way to make it so it could output to a midi
device"*. The app currently only speaks QWERTY, so a user with a real synth,
a DAW, or a soft-synth like VirtualMIDISynth cannot use it at all. It also
sidesteps every keystroke-protocol problem in one move: no ALT velocity tap, no
scancode collisions between notes an octave apart, no `ctrl+w` closing a
browser tab.

## The seam

Directory ownership is too coarse here, so this names files and cases.

**Engine half.** `MIDI++/` and `tests/`: new files `MidiOutput.hpp` /
`MidiOutput.cpp`, and edits to `MIDI2Key.cpp`, `PlaybackCore.cpp`,
`PlaybackSystem.hpp`.

**Panel half.** `ui/`: `ShellEngine::Action`, `EngineSnapshot`, `Panels.cpp`.
Whoever owns the panel owns those two types; the engine half consumes them and
never extends them. The exact additions are listed under "What the panel half
adds" so they can be written once, by their owner, rather than twice.

Do not run both halves concurrently in the same file. `PlaybackSystem.hpp` is
engine-side; `ShellEngine.hpp` is panel-side; nothing else is touched by both.

## What the engine half adds

### `IMidiOutput`

Mirror `IMidiInput` field for field, including the reasoning in its header
comment about opaque ids:

```cpp
class IMidiOutput {
public:
    virtual ~IMidiOutput() = default;
    virtual MidiBackend backend() const noexcept = 0;
    virtual std::vector<MidiInputDevice> enumerate() = 0;   // same struct, ids and names
    virtual bool open(const std::wstring& deviceId) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const noexcept = 0;
    virtual const std::wstring& openedDeviceId() const noexcept = 0;
    virtual void send(const uint8_t* message, size_t length) = 0;
};

std::unique_ptr<IMidiOutput> CreateMidiOutput(MidiBackend backend);
std::vector<MidiInputDevice> EnumerateMidiOutputs();
MidiBackend BackendForOutputId(const std::wstring& deviceId);
```

Backends: WinRT `Windows.Devices.Midi.MidiOutPort`, and WinMM through the
vendored RtMidi's `RtMidiOut`. Two is enough. Kernel Streaming buys nothing on
the output side, because the reason it exists on input is removing a
marshalling hop from the latency path and nothing is waiting on output.

Reuse the `winmm:<index>|<name>` id shape and `ResolveWinMMPort`'s rule: an id
naming a device that is not present opens **nothing**, never a different port.
That rule is why the input side stopped opening the wrong keyboard, and output
ids renumber on unplug for exactly the same reason.

`send()` is called from the MIDI callback thread and from the playback thread.
It must not allocate and must not block; RtMidi's `sendMessage` and WinRT's
`SendBuffer` are both fine, a `std::vector` built per note is not.

### The target switch

On `VirtualPianoPlayer`, engine-side:

```cpp
enum class OutputTarget { Keystrokes, MidiDevice };
std::atomic<OutputTarget> output_target{OutputTarget::Keystrokes};
```

Two call sites branch on it, and they are the only two that know a note number
rather than a scancode:

1. **`MIDI2Key::ProcessMidiMessage`**. It already holds the raw MIDI bytes.
   On the MIDI target, apply transpose to `bytes[1]` and forward the message
   verbatim. Do not route it through `g_adjustedNote`: folding out-of-range
   notes into the 61-key window exists because the *game* has 61 keys, and a
   real MIDI device has 128. Same for the 88-key mode flag. Both are
   keystroke-path concepts and must not reach the wire.

2. **`VirtualPianoPlayer::execute_note_event`** (`PlaybackCore.cpp`, around
   line 1875). It holds a note *name* and a velocity. Needs name → number,
   which is the inverse of `NOTE_NAME_CACHE`; build the reverse table once
   rather than parsing the string per note. Emit `0x90 note velocity` on
   Press, `0x80 note 0` on Release, and `0xB0 64 (0|127)` for sustain.

Suppress the ALT velocity tap entirely on the MIDI target. Velocity travels in
the note-on byte there, so the tap is not merely unnecessary, it would be a
phantom note, and that is the bug this whole report started with, see the note
below.

### Switching targets is a release

This is the part that will be got wrong if it is not written down.

Keys go down on one target and must come up on the same one. Switching while a
note is held strands it: a keystroke stays down in the game, or a MIDI note
sounds forever on the synth. So the switch itself has to release first, in this
order, and nothing may inject between the two steps:

1. Release everything held on the **outgoing** target: `release_all_keys()`
   for keystrokes, or All Notes Off (`0xB0 123 0`) plus sustain off
   (`0xB0 64 0`) on every channel in use for MIDI.
2. Then store the new `output_target`.

`release_all_keys()` and the F4 panic path both need the same branch, or the
panic key stops being a panic key on the MIDI target. `MIDI2Key`'s own
`pressed[]` and `scancodeOwner[]` bookkeeping is keystroke-only state and must
be cleared on the way out, not left to be reinterpreted later.

Closing the output port, losing the device, and stopping playback all count as
switching away.

## What the panel half adds

Written here so it is added once, by the seat that owns these types. Mirror the
live-input members exactly, same names with `output` for `live`, because the
picker is the same picker and a reviewer should be able to diff them.

`ShellEngine::Action`:

- `OutputTarget`: `command.value` is true for MIDI, false for keystrokes.
- `OutputScan`: refill the device list, same as `LiveScan`.
- `OutputOpen`: `command.device` is the id, empty closes.

`EngineSnapshot`:

- `bool outputMidi = false;`, so keystrokes stay the default.
- `std::wstring outputDevice;`
- `std::vector<LiveDevice> outputDevices;`, the existing row type, reused
  rather than duplicated; a row is an id and a name either way.

Panel: a two-way choice next to the existing MIDI input picker, with the device
combo disabled while Keystrokes is selected. When MIDI is the target, the
Velocity control should read as unavailable rather than off, because velocity
is still being sent, just not as keystrokes.

## Tests

`tests/ShellTests.cpp`, which never types into the desktop, and needs a fake
`IMidiOutput` that records messages, the same shape as the `InjectInput =
Capture` seam already at the top of `wmain`.

Assert, at minimum:

- A note on the MIDI target records a MIDI message and injects **no** INPUT.
- A note on the keystroke target injects INPUT and records **no** MIDI message.
- Switching targets with a note held releases it on the outgoing target,
  before anything is sent on the new one.
- Out-of-range and black notes reach the wire unfolded and unmapped, including
  notes the 61-key layout has no character for.
- An output id naming an absent device opens nothing.
- Velocity reaches the note-on byte, and no ALT tap is injected.

## One thing this does not fix

The reports that started this, double notes and `alt+1` triggering Roblox
capture, are the ALT velocity tap on the *keystroke* path, not an output
problem. `954b3cd` turned that off by default. Routing to MIDI avoids it
because there is no tap on the wire, but the keystroke path still has it for
anyone who turns velocity back on, and making the modifier configurable is a
separate piece of work. Do not close those reports on the strength of this one.
