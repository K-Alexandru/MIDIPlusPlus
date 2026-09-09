#pragma once

// MidiOutput: one interface in front of every MIDI output transport.
//
// Deliberately the same shape as IMidiInput, because it is the same problem
// one direction over and the input side already paid for the mistakes. Read
// MidiInput.hpp first; the reasoning there about opaque ids applies here
// unchanged, and output ports renumber on unplug for the same reason input
// ports do.
//
// Two backends and no more. Kernel Streaming exists on the input side to take
// a marshalling hop out of the latency path, and nothing is waiting on output,
// so it would buy nothing here.

#define NOMINMAX

#include "MidiInput.hpp"   // MidiBackend and MidiInputDevice, shared on purpose

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class IMidiOutput {
public:
    virtual ~IMidiOutput() = default;

    virtual MidiBackend backend() const noexcept = 0;

    // The same row type as the input side. A row is an id and a name either
    // way, and duplicating the struct would mean the panel needed two pickers
    // that differ in nothing a reviewer could point at.
    virtual std::vector<MidiInputDevice> enumerate() = 0;

    // Opening replaces any port this instance already had open.
    virtual bool open(const std::wstring& deviceId) = 0;
    virtual void close() = 0;

    virtual bool isOpen() const noexcept = 0;
    virtual const std::wstring& openedDeviceId() const noexcept = 0;

    // Called from the MIDI callback thread and from the playback thread, so it
    // allocates nothing: the WinRT backend writes into a buffer taken once at
    // open, and the WinMM backend uses RtMidi's pointer-and-length overload
    // rather than building a std::vector per note.
    //
    // It does take a short mutex, which the spec's "must not block" was not
    // asking us to skip. Two threads interleaving bytes into one port is a
    // corrupted MIDI stream, the lock is uncontended whenever only one of live
    // input and autoplay is running, and it is held for the length of a
    // three-byte write to a handle. Blocking would be waiting on the UI or on
    // I/O, and this does neither.
    virtual void send(const uint8_t* message, size_t length) = 0;
};

std::unique_ptr<IMidiOutput> CreateMidiOutput(MidiBackend backend);

// Device list for the UI. The ids carry their own backend, so a caller can
// open whatever it picked without tracking which list an entry came from.
std::vector<MidiInputDevice> EnumerateMidiOutputs();

// Which backend produced this id. Defaults to WinRT for empty or unknown ids.
MidiBackend BackendForOutputId(const std::wstring& deviceId);

// Tests substitute a transport here. Same pattern and the same reason as
// SetMidiInputFactory and InjectInput: the indirection exists only so the thing
// under test can be driven, and the default is the real backend. Passing {}
// restores it. Set it before opening a device and leave it alone afterwards;
// nothing synchronises it, because nothing but a test ever writes it.
using MidiOutputFactory = std::function<std::unique_ptr<IMidiOutput>(MidiBackend)>;
void SetMidiOutputFactory(MidiOutputFactory factory);

// The MIDI number for a note name such as "C4" or "A#3", or -1 when the name
// is not one. Exposed because execute_note_event holds a name where the wire
// wants a number, and building the reverse of NOTE_NAME_CACHE once beats
// parsing the string per note.
//
// It is the inverse of that table and not a second opinion about it: the test
// walks all 128 numbers through both and requires a round trip.
int MidiNumberForNoteName(const char* name) noexcept;
