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
