#pragma once
#include "PlaybackSystem.hpp"
#include <atomic>
#include <array>
#include <vector>
#include "MidiInput.hpp"
#include <memory>
#include <string>
#include "InputHeader.h"
#include <windows.h>
#include <timeapi.h>

#define CACHE_LINE_SIZE 64

// MIDI2Key.hpp defines MAX_BATCH_INPUTS as a macro. When that header is included
// first (as in MIDI++.cpp) the macro rewrites the class constant below into
// "static constexpr size_t 32 = 32;". Drop the macro before declaring the class.
#ifdef MAX_BATCH_INPUTS
#undef MAX_BATCH_INPUTS
#endif

class MIDIConnect {
public:
    MIDIConnect();
    ~MIDIConnect();

    void OpenDevice(const std::wstring& deviceId);
    void CloseDevice();
    inline bool IsActive() const { return m_isActive.load(std::memory_order_relaxed); }
    inline const std::wstring& GetSelectedDevice() const { return m_selectedDevice; }
    void SetActive(bool active);
    void ReleaseAllNumpadKeys();

private:
    // One message from whichever transport is open. The INPUT batch is a local,
    // not a shared member, so concurrent callbacks cannot scribble over each
    // other and no re-entrancy guard has to drop messages to stay safe.
    void HandleMessage(uint64_t timestampQpc, const uint8_t* data, size_t length);

    static constexpr struct {
        WORD down;
        WORD up;
    } NUMPAD_SCANCODES[12] = {
        {0x52, 0x52}, {0x4F, 0x4F}, {0x50, 0x50}, {0x51, 0x51},
        {0x4B, 0x4B}, {0x4C, 0x4C}, {0x4D, 0x4D}, {0x47, 0x47},
        {0x48, 0x48}, {0x49, 0x49}, {0x4A, 0x4A}, {0x4E, 0x4E}
    };

    static constexpr size_t MAX_BATCH_INPUTS = 32;
    alignas(CACHE_LINE_SIZE) std::array<std::array<std::array<INPUT, 10>, 128>, 128> m_noteMapping;
    alignas(CACHE_LINE_SIZE) std::array<std::array<INPUT, 10>, 128> m_sustainMapping;

    std::unique_ptr<IMidiInput> m_input;
    std::wstring m_selectedDevice;
    std::atomic<bool> m_isActive;

    static HANDLE s_mmcssHandle;
    static DWORD s_mmcssTaskIndex;
    static DWORD_PTR s_originalAffinity;
    static ULONG s_timerResolution;

    static bool OptimizeSystem();
    static void RestoreSystemDefaults();
    static void SetCallbackThreadPriority();
};