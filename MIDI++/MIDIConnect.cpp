#include "MIDIConnect.hpp"
#include "InputHeader.h"
#include <iostream>
#include <cstring>

namespace {
    static const uint8_t div12[128] = {
        0,0,0,0,0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1,1,1,1,1, 2,2,2,2,2,2,2,2,2,2,2,2,
        3,3,3,3,3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4,4,4,4,4, 5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6, 7,7,7,7,7,7,7,7,7,7,7,7, 8,8,8,8,8,8,8,8,8,8,8,8,
        9,9,9,9,9,9,9,9,9,9,9,9, 10,10,10,10,10,10,10,10
    };

    static const uint8_t mod12[128] = {
        0,1,2,3,4,5,6,7,8,9,10,11, 0,1,2,3,4,5,6,7,8,9,10,11, 0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11, 0,1,2,3,4,5,6,7,8,9,10,11, 0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11, 0,1,2,3,4,5,6,7,8,9,10,11, 0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11, 0,1,2,3,4,5,6,7
    };

    INPUT __forceinline MakeKeyboardInput(WORD scanCode, DWORD flags) {
        INPUT inp{};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = 0;
        inp.ki.wScan = scanCode;
        inp.ki.dwFlags = flags;
        inp.ki.time = 0;
        inp.ki.dwExtraInfo = 0;
        return inp;
    }
}

HANDLE MIDIConnect::s_mmcssHandle = NULL;
DWORD MIDIConnect::s_mmcssTaskIndex = 0;
DWORD_PTR MIDIConnect::s_originalAffinity = 0;
ULONG MIDIConnect::s_timerResolution = 0;

MIDIConnect::MIDIConnect()
    : m_selectedDevice()
    , m_isActive(false)
{
    OptimizeSystem();
    // The mapping tables are plain INPUT arrays, so wVk, time and dwExtraInfo
    // are indeterminate until something writes them. Only wScan, dwFlags and
    // type are filled in below, and the whole struct is what gets sent.
    std::memset(m_noteMapping.data(), 0, sizeof(m_noteMapping));
    std::memset(m_sustainMapping.data(), 0, sizeof(m_sustainMapping));

    // Precompute note mappings - hot path optimization
    for (int note = 0; note < 128; ++note) {
        for (int vel = 0; vel < 128; ++vel) {
            INPUT* mapping = m_noteMapping[note][vel].data();
            constexpr DWORD SC_FLAG = KEYEVENTF_SCANCODE;
            constexpr DWORD KU_FLAG = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            mapping[0].type = INPUT_KEYBOARD;
            mapping[0].ki.wScan = 0x37;  // Numpad *
            mapping[0].ki.dwFlags = SC_FLAG;
            mapping[1].type = INPUT_KEYBOARD;
            mapping[1].ki.wScan = 0x37;
            mapping[1].ki.dwFlags = KU_FLAG;

            auto noteOct = NUMPAD_SCANCODES[div12[note]];
            auto noteVal = NUMPAD_SCANCODES[mod12[note]];

            mapping[2].type = INPUT_KEYBOARD;
            mapping[2].ki.wScan = noteOct.down;
            mapping[2].ki.dwFlags = SC_FLAG;
            mapping[3].type = INPUT_KEYBOARD;
            mapping[3].ki.wScan = noteOct.up;
            mapping[3].ki.dwFlags = KU_FLAG;
            mapping[4].type = INPUT_KEYBOARD;
            mapping[4].ki.wScan = noteVal.down;
            mapping[4].ki.dwFlags = SC_FLAG;
            mapping[5].type = INPUT_KEYBOARD;
            mapping[5].ki.wScan = noteVal.up;
            mapping[5].ki.dwFlags = KU_FLAG;

            auto velOct = NUMPAD_SCANCODES[div12[vel]];
            auto velVal = NUMPAD_SCANCODES[mod12[vel]];

            mapping[6].type = INPUT_KEYBOARD;
            mapping[6].ki.wScan = velOct.down;
            mapping[6].ki.dwFlags = SC_FLAG;
            mapping[7].type = INPUT_KEYBOARD;
            mapping[7].ki.wScan = velOct.up;
            mapping[7].ki.dwFlags = KU_FLAG;
            mapping[8].type = INPUT_KEYBOARD;
            mapping[8].ki.wScan = velVal.down;
            mapping[8].ki.dwFlags = SC_FLAG;
            mapping[9].type = INPUT_KEYBOARD;
            mapping[9].ki.wScan = velVal.up;
            mapping[9].ki.dwFlags = KU_FLAG;
        }
    }

    // Precompute sustain mappings
    constexpr BYTE SUSTAIN_NOTE = 143;
    auto sOct = NUMPAD_SCANCODES[SUSTAIN_NOTE / 12];
    auto sVal = NUMPAD_SCANCODES[SUSTAIN_NOTE % 12];

    for (int val = 0; val < 128; ++val) {
        INPUT* mapping = m_sustainMapping[val].data();
        constexpr DWORD SC_FLAG = KEYEVENTF_SCANCODE;
        constexpr DWORD KU_FLAG = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        mapping[0].type = INPUT_KEYBOARD;
        mapping[0].ki.wScan = 0x37;
        mapping[0].ki.dwFlags = SC_FLAG;
        mapping[1].type = INPUT_KEYBOARD;
        mapping[1].ki.wScan = 0x37;
        mapping[1].ki.dwFlags = KU_FLAG;

        mapping[2].type = INPUT_KEYBOARD;
        mapping[2].ki.wScan = sOct.down;
        mapping[2].ki.dwFlags = SC_FLAG;
        mapping[3].type = INPUT_KEYBOARD;
        mapping[3].ki.wScan = sOct.up;
        mapping[3].ki.dwFlags = KU_FLAG;
        mapping[4].type = INPUT_KEYBOARD;
        mapping[4].ki.wScan = sVal.down;
        mapping[4].ki.dwFlags = SC_FLAG;
        mapping[5].type = INPUT_KEYBOARD;
        mapping[5].ki.wScan = sVal.up;
        mapping[5].ki.dwFlags = KU_FLAG;

        auto valOct = NUMPAD_SCANCODES[div12[val]];
        auto valVal = NUMPAD_SCANCODES[mod12[val]];

        mapping[6].type = INPUT_KEYBOARD;
        mapping[6].ki.wScan = valOct.down;
        mapping[6].ki.dwFlags = SC_FLAG;
        mapping[7].type = INPUT_KEYBOARD;
        mapping[7].ki.wScan = valOct.up;
        mapping[7].ki.dwFlags = KU_FLAG;
        mapping[8].type = INPUT_KEYBOARD;
        mapping[8].ki.wScan = valVal.down;
        mapping[8].ki.dwFlags = SC_FLAG;
        mapping[9].type = INPUT_KEYBOARD;
        mapping[9].ki.wScan = valVal.up;
        mapping[9].ki.dwFlags = KU_FLAG;
    }
}

MIDIConnect::~MIDIConnect() {
    CloseDevice();
    RestoreSystemDefaults();
}

bool MIDIConnect::OptimizeSystem() {
    TIMECAPS tc;
    timeGetDevCaps(&tc, sizeof(TIMECAPS));
    s_timerResolution = std::min(std::max(tc.wPeriodMin, (UINT)1), tc.wPeriodMax);
    timeBeginPeriod(s_timerResolution);
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);

    if (HANDLE hProcess = GetCurrentProcess()) {
        const ULONG EXECUTION_SPEED_MASK = 0x1;
        struct PowerThrottlingState {
            ULONG Version;
            ULONG ControlMask;
            ULONG StateMask;
        };
        PowerThrottlingState powerThrottling{};
        powerThrottling.Version = 0x1;
        powerThrottling.ControlMask = EXECUTION_SPEED_MASK;
        powerThrottling.StateMask = 0; // UNLEASH THE BEAST - NO THROTTLING
        typedef BOOL(WINAPI* SetProcessInfoPtr)(HANDLE, INT, LPVOID, DWORD);
        if (HMODULE kernel32 = GetModuleHandleA("kernel32.dll")) {
            if (auto SetProcessInformation = (SetProcessInfoPtr)GetProcAddress(kernel32, "SetProcessInformation")) {
                SetProcessInformation(hProcess, 4, &powerThrottling, sizeof(powerThrottling));
            }
        }
    }
    return true;
}

void MIDIConnect::RestoreSystemDefaults() {
    if (s_mmcssHandle != NULL) {
        AvRevertMmThreadCharacteristics(s_mmcssHandle);
        s_mmcssHandle = NULL;
    }

    if (s_timerResolution > 0) {
        timeEndPeriod(s_timerResolution);
        s_timerResolution = 0;
    }

    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
}

void MIDIConnect::SetCallbackThreadPriority() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    DWORD taskIndex = 0;
    HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (mmcssHandle != NULL) {
        if (AvSetMmThreadPriority(mmcssHandle, AVRT_PRIORITY_CRITICAL)) {
            s_mmcssHandle = mmcssHandle;
            s_mmcssTaskIndex = taskIndex;
        }
        else {
            AvRevertMmThreadCharacteristics(mmcssHandle);
        }
    }

    DWORD_PTR systemMask;
    if (GetProcessAffinityMask(GetCurrentProcess(), &s_originalAffinity, &systemMask)) {
        SetThreadAffinityMask(GetCurrentThread(), 1); // Pin to first core - lowest latency
    }

    typedef enum _THREADINFOCLASS {
        ThreadIoPriority = 31,
    } THREADINFOCLASS;
    typedef enum _IO_PRIORITY_HINT {
        IoPriorityCritical = 3
    } IO_PRIORITY_HINT;
    typedef DWORD(WINAPI* NtSetThreadInfoPtr)(HANDLE, THREADINFOCLASS, PVOID, ULONG);
    if (HMODULE ntdll = GetModuleHandleA("ntdll.dll")) {
        if (auto NtSetInformationThread = (NtSetThreadInfoPtr)GetProcAddress(ntdll, "NtSetInformationThread")) {
            IO_PRIORITY_HINT ioPriority = IoPriorityCritical;
            NtSetInformationThread(GetCurrentThread(), ThreadIoPriority, &ioPriority, sizeof(ioPriority));
        }
    }
}
void MIDIConnect::OpenDevice(const std::wstring& deviceId) {
    CloseDevice();
    if (deviceId.empty()) return;

    m_input = CreateMidiInput(BackendForDeviceId(deviceId));
    if (!m_input) return;

    const bool opened = m_input->open(deviceId,
        [this](uint64_t timestampQpc, const uint8_t* data, size_t length) {
            this->HandleMessage(timestampQpc, data, length);
        });
    if (!opened) {
        m_input.reset();
        return;
    }
    SetCallbackThreadPriority();
    m_selectedDevice = deviceId;
}

void MIDIConnect::CloseDevice() {
    if (m_input) {
        m_input->close();
        m_input.reset();
    }
    m_selectedDevice.clear();
}

void MIDIConnect::SetActive(bool active) {
    bool wasActive = m_isActive.load(std::memory_order_relaxed);
    m_isActive.store(active, std::memory_order_release);

    if (active && !wasActive) {
        if (SyscallNumber == 0) {
            try {
                SyscallNumber = GetNtUserSendInputSyscallNumber();
                InitializeNtUserSendInputCall();
            }
            catch (const std::exception& e) {
                m_isActive.store(false, std::memory_order_release);
                return;
            }
        }
    }
}

void MIDIConnect::ReleaseAllNumpadKeys() {
    INPUT inputs[12] = {};
    static const WORD numpadScans[12] = {
        0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x37, 0x4A
    };

    for (int i = 0; i < 12; ++i) {
        inputs[i].type = INPUT_KEYBOARD;
        inputs[i].ki.wScan = numpadScans[i];
        inputs[i].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    }
    input_latency::send(12, inputs, sizeof(INPUT));
}

void MIDIConnect::HandleMessage(uint64_t timestampQpc, const uint8_t* data, size_t length)
{
    if (!data || length < 3) return;
    if (!m_isActive.load(std::memory_order_relaxed)) return;
    if (data[1] > 127 || data[2] > 127) return;

    const uint8_t status = data[0];
    const uint8_t data1 = data[1];
    const uint8_t data2 = data[2];
    const uint8_t cmd = status & 0xF0;
    if (cmd != 0x90 && cmd != 0x80 && !(cmd == 0xB0 && data1 == 64)) return;
    input_latency::Trace trace(input_latency::Source::MidiConnect,
        cmd == 0xB0 ? input_latency::Kind::Sustain :
        cmd == 0x90 && data2 > 0 ? input_latency::Kind::NoteOn : input_latency::Kind::NoteOff,
        timestampQpc);

    // Local batch: two callbacks can now run at once without fighting over one
    // buffer, which is what the old m_inCallback guard was papering over by
    // dropping every message that arrived during another.
    INPUT batched[MAX_BATCH_INPUTS];
    size_t inputCount = 0;

    switch (cmd) {
    case 0x90: // Note On
        if (data2 > 0) {
            const auto& mapping = m_noteMapping[data1][data2];
            for (int i = 0; i < 10 && inputCount < MAX_BATCH_INPUTS; i++) {
                batched[inputCount++] = mapping[i];
            }
        }
        else {
            const auto& mapping = m_noteMapping[data1][0];
            for (int i = 0; i < 10 && inputCount < MAX_BATCH_INPUTS; i++) {
                batched[inputCount++] = mapping[i];
            }
        }
        break;

    case 0x80: { // Note Off
        const auto& mapping = m_noteMapping[data1][0];
        for (int i = 0; i < 10 && inputCount < MAX_BATCH_INPUTS; i++) {
            batched[inputCount++] = mapping[i];
        }
        break;
    }

    case 0xB0: // Control Change
        if (data1 == 64) { // Sustain pedal
            const auto& mapping = m_sustainMapping[data2];
            for (int i = 0; i < 10 && inputCount < MAX_BATCH_INPUTS; i++) {
                batched[inputCount++] = mapping[i];
            }
        }
        break;
    }

    if (inputCount > 0) input_latency::send(static_cast<UINT>(inputCount), batched, sizeof(INPUT));
}
