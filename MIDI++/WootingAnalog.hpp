#pragma once

// Wooting analog keys as a MIDI input source.
//
// Today the user's notes travel analog key -> wooting-analog-midi (a separate
// Rust app) -> teVirtualMIDI virtual port -> this app. Two processes and a
// virtual MIDI driver sit in front of us and their latency gets blamed on us.
// Reading the analog SDK directly removes all of it.
//
// The SDK is loaded at runtime with LoadLibrary, so the app still starts on a
// machine that has never seen a Wooting keyboard, and we need no import library
// or vendored headers. Licence is MPL-2.0, which is GPLv3-compatible.

#include "MidiInput.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>

std::unique_ptr<IMidiInput> CreateWootingAnalogInput();

// True when wooting_analog_sdk.dll is present and reports at least one device.
bool WootingAnalogAvailable();

// Scancode (set 1) to MIDI note, -1 for "not a note key". The default is the
// virtual-piano layout the app already types out, so a key plays the note it
// would otherwise produce. Replace it to follow a user's own key mapping.
void SetWootingScancodeNoteMap(const std::array<int16_t, 256>& map);
std::array<int16_t, 256> DefaultWootingScancodeNoteMap();

// The same map built from the user's own KEY_MAPPINGS.FULL, note name to key,
// so a Wooting key sounds the note the app would type for it. Only unshifted
// single-character bindings can be carried: a shifted key is two physical keys
// to the analog SDK, so there is no one scancode to hang the note on.
std::array<int16_t, 256> WootingScancodeNoteMapFrom(
    const std::map<std::string, std::string>& keyMappings);

// How the backend turns key travel into notes. Mirrors the three controls
// wooting-analog-midi exposes, whose absence is why a Wooting here could only
// play the white keys of one fixed table at one fixed sensitivity. Defaults
// match that app. Config carries these as WOOTING_ANALOG; see config.hpp for
// the field-by-field explanation and the accepted ranges.
struct WootingAnalogSettings {
    float trigger = 0.5f;
    float releaseFraction = 0.6f;
    int shiftAmount = 12;
    float velocityScale = 5.0f;
};

// Safe to call while a device is open: the poll loop reads a copy each pass.
void SetWootingAnalogSettings(const WootingAnalogSettings& settings);
WootingAnalogSettings GetWootingAnalogSettings();

// Set 1 scancode of the key that applies the shift, and the only key the
// backend reads that is not a note.
inline constexpr uint16_t kWootingShiftScancode = 0x2A; // Left Shift

// MIDI velocity for a key that travelled from previousDepth to depth in the
// given number of seconds. Free rather than private to the poll loop so the
// scale can be driven directly by a test: the loop around it needs a keyboard,
// and this does not.
uint8_t WootingVelocityFor(float depth, float previousDepth, double seconds, float velocityScale);

// What one poll of the analog buffer decides, with no SDK and no clock in it.
//
// The loop that calls this needs a Wooting on the desk; this does not, and it
// is where everything that can be got wrong lives: the trigger and its release
// gap, which note a shifted key sounds, and which note a key that is let go has
// to release.
struct WootingPollState {
    std::array<float, 256> lastDepth{};
    // The note each key is currently sounding, or -1 for a key that is up. It
    // has to be the note actually sent, not the note the map gives now: the
    // shift can be let go while the key is still held, and a note off that does
    // not match its note on leaves the game holding a key down forever.
    std::array<int16_t, 256> sounding{};

    WootingPollState() { lastDepth.fill(0.0f); sounding.fill(-1); }
};

struct WootingPollEvent {
    bool on = false;
    uint8_t note = 0;
    uint8_t velocity = 0;   // 0 on a note off
};

// codes and values are the SDK's buffer for this poll, count its length.
// seconds is the time since the previous poll, used only for strike speed.
// Writes at most 2 * count + 256 events, so out must hold that; returns how
// many were written. Allocates nothing: this runs at 1kHz on its own thread.
size_t WootingPollStep(WootingPollState& state,
                       const uint16_t* codes, const float* values, int count,
                       const std::array<int16_t, 256>& noteMap,
                       const WootingAnalogSettings& settings,
                       double seconds,
                       WootingPollEvent* out, size_t outCapacity);
