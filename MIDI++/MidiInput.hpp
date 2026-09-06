#pragma once

// MidiInput: one interface in front of every MIDI input transport.
//
// Why this exists: three index spaces used to disagree. The device combo listed
// WinMM devices minus the ones that failed an access test, MIDI2Key indexed the
// WinRT collection, and MIDIConnect indexed RtMidi's port list. With one device
// present they happened to agree; with a piano plus loopMIDI they did not, and
// the app opened the wrong port. Devices are now identified by an opaque string
// that only the backend that produced it has to understand.

#define NOMINMAX

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum class MidiBackend {
    WinRT,   // Windows.Devices.Midi, Windows 10+
    WinMM,   // legacy midiIn*, through the vendored RtMidi
    WootingAnalog // analog key depth read straight from the Wooting SDK
};

struct MidiInputDevice {
    std::wstring id;     // opaque, backend-specific, stable enough to reopen with
    std::wstring name;   // for display
    MidiBackend  backend = MidiBackend::WinRT;
};

// timestampQpc is QueryPerformanceCounter ticks taken on entry to the backend
// callback: t0 of the latency chain described in HANDOFF section 7. data points
// at one complete MIDI message and is only valid for the duration of the call.
using MidiInputCallback = std::function<void(uint64_t timestampQpc,
                                             const uint8_t* data,
                                             size_t length)>;

class IMidiInput {
public:
    virtual ~IMidiInput() = default;

    virtual MidiBackend backend() const noexcept = 0;
    virtual std::vector<MidiInputDevice> enumerate() = 0;

    // Opening replaces any port this instance already had open.
    virtual bool open(const std::wstring& deviceId, MidiInputCallback callback) = 0;
    virtual void close() = 0;

    virtual bool isOpen() const noexcept = 0;
    virtual const std::wstring& openedDeviceId() const noexcept = 0;
};

std::unique_ptr<IMidiInput> CreateMidiInput(MidiBackend backend);

// Device list for the UI: WinRT if it reports anything, WinMM otherwise. The
// ids carry their own backend, so a caller can open whatever it picked without
// tracking which list an entry came from.
std::vector<MidiInputDevice> EnumerateMidiInputs();

// Which backend produced this id. Defaults to WinRT for empty or unknown ids.
MidiBackend BackendForDeviceId(const std::wstring& deviceId);

// RtMidi appends " <index>" to every WinMM port name, so that two keyboards of
// the same model are still told apart. That puts the index inside the name,
// which is the one half of a WinMM id meant to outlive renumbering: unplug a
// device and every port after it shifts down, the stored name stops matching,
// and resolution falls back to the index that just moved. Names are therefore
// compared, and stored in ids, with that suffix removed. A device genuinely
// named "Piano 2" keeps its number, because RtMidi's own suffix is always the
// last token and only one token is taken.
std::wstring StripRtMidiPortIndex(const std::wstring& name);

// The port index a WinMM device id names, or -1 when that device is not
// present. Exposed for tests: the resolution is the part that goes wrong with
// two devices, and the loop around it needs the hardware.
//
// It never guesses. Opening a different keyboard from the one asked for is
// worse than opening nothing, because nothing is visible and wrong is not.
int ResolveWinMMPort(const std::wstring& deviceId, const std::vector<std::wstring>& portNames);
