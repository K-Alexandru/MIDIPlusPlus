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

    // Releases every note this instance believes is down and clears the
    // scancode bookkeeping behind it. Registered with the player as its live
    // release hook, so switching the output target releases the live path's
    // keys in the same call that releases autoplay's, before anything can be
    // sent on the new target.
    //
    // Safe to call when nothing is held, which is the usual case.
    void ReleaseHeldKeys();

private:
    void ProcessMidiMessage(uint64_t timestampQpc, const uint8_t* data, size_t length);

    // Rebuilds the Wooting scancode-to-note map from whichever layout is
    // active, 88-key or 61-key. Does nothing for any other backend.
    void ApplyWootingLayout(const std::wstring& deviceId);

    // Transport, chosen per device id
    std::unique_ptr<IMidiInput> m_input;
    std::wstring m_selectedDevice;


    int m_selectedChannel;
    std::atomic<bool> m_isActive;
    VirtualPianoPlayer* m_player; // copy 

    // Key injection buffers
    alignas(64) static char m_lastVelocityKey;


    // For each note [0..127], track if it is pressed
    alignas(64) std::array<std::atomic<bool>, 128> pressed;
};

