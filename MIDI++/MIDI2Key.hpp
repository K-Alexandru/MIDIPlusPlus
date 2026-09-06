#pragma once

#define NOMINMAX

#include <atomic>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// MIDI transport lives behind IMidiInput, so this class no longer knows or
// cares whether the bytes arrived over WinRT or WinMM.
#include "MidiInput.hpp"

// Classic Windows
#include <windows.h>

// Forward declarations
#include "PlaybackSystem.hpp"

struct PrecomputedKeyEvents;

//--------------------------------------------------------------------------------
// MIDI2Key: Pain and Suffering
//--------------------------------------------------------------------------------
class MIDI2Key {
public:
    MIDI2Key(VirtualPianoPlayer* player);
    ~MIDI2Key();

    void OpenDevice(const std::wstring& deviceId);
    void CloseDevice();
    void SetMidiChannel(int channel);

    bool IsActive() const;
    void SetActive(bool active);

    const std::wstring& GetSelectedDevice() const;
    int GetSelectedChannel() const;

private:
    void ProcessMidiMessage(uint64_t timestampQpc, const uint8_t* data, size_t length);

    // Transport, chosen per device id
    std::unique_ptr<IMidiInput> m_input;
    std::wstring m_selectedDevice;


    int m_selectedChannel;
    std::atomic<bool> m_isActive;
    VirtualPianoPlayer* m_player; // copy 

    // Key injection buffers
    alignas(64) static INPUT m_sustainInput[2];
    alignas(64) static char m_lastVelocityKey;

    // Modifier usage counters (e.g., alt, ctrl, shift)
    alignas(64) static std::atomic<int> modifierCounts[3];

    // For each note [0..127], track if it is pressed
    alignas(64) std::array<std::atomic<bool>, 128> pressed;
};

