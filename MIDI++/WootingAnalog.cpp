// WootingAnalog.cpp: analog key positions turned into MIDI note messages.

#include "WootingAnalog.hpp"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---- SDK binding -----------------------------------------------------------
// Prototypes copied from the wooting-analog-sdk C API. Bound by name at runtime
// so a missing DLL is a "no device" answer rather than a failed start.

enum WootingAnalog_KeycodeType : uint32_t {
    HID = 0,
    ScanCode1 = 1,
    VirtualKey = 2,
    VirtualKeyTranslate = 3,
};

using pfn_initialise = int (*)();
using pfn_is_initialised = bool (*)();
using pfn_uninitialise = int (*)();
using pfn_set_keycode_mode = int (*)(WootingAnalog_KeycodeType);
using pfn_read_full_buffer = int (*)(uint16_t* codeBuffer, float* analogBuffer, unsigned int length);

struct Sdk {
    HMODULE module = nullptr;
    pfn_initialise initialise = nullptr;
    pfn_is_initialised isInitialised = nullptr;
    pfn_uninitialise uninitialise = nullptr;
    pfn_set_keycode_mode setKeycodeMode = nullptr;
    pfn_read_full_buffer readFullBuffer = nullptr;

    bool bound() const {
        return module && initialise && isInitialised && uninitialise && setKeycodeMode && readFullBuffer;
    }
};

Sdk& sdk() {
    static Sdk s = [] {
        Sdk out;
        // The installer puts the DLL in Program Files and does not add it to
        // PATH, so try the known location as well as the loader's own search.
        const wchar_t* candidates[] = {
            L"wooting_analog_sdk.dll",
            L"C:\\Program Files\\wooting-analog-sdk\\wooting_analog_sdk.dll",
            L"C:\\Program Files (x86)\\wooting-analog-sdk\\wooting_analog_sdk.dll",
        };
        for (const wchar_t* path : candidates) {
            out.module = LoadLibraryW(path);
            if (out.module) break;
        }
        if (!out.module) return out;

        out.initialise = reinterpret_cast<pfn_initialise>(
            GetProcAddress(out.module, "wooting_analog_initialise"));
        out.isInitialised = reinterpret_cast<pfn_is_initialised>(
            GetProcAddress(out.module, "wooting_analog_is_initialised"));
        out.uninitialise = reinterpret_cast<pfn_uninitialise>(
            GetProcAddress(out.module, "wooting_analog_uninitialise"));
        out.setKeycodeMode = reinterpret_cast<pfn_set_keycode_mode>(
            GetProcAddress(out.module, "wooting_analog_set_keycode_mode"));
        out.readFullBuffer = reinterpret_cast<pfn_read_full_buffer>(
            GetProcAddress(out.module, "wooting_analog_read_full_buffer"));
        return out;
    }();
    return s;
}

// ---- default note map ------------------------------------------------------
// Set 1 scancodes for the rows the virtual-piano layout uses.
constexpr uint16_t SC_1 = 0x02, SC_2 = 0x03, SC_3 = 0x04, SC_4 = 0x05, SC_5 = 0x06;
constexpr uint16_t SC_6 = 0x07, SC_7 = 0x08, SC_8 = 0x09, SC_9 = 0x0A, SC_0 = 0x0B;
constexpr uint16_t SC_Q = 0x10, SC_W = 0x11, SC_E = 0x12, SC_R = 0x13, SC_T = 0x14;
constexpr uint16_t SC_Y = 0x15, SC_U = 0x16, SC_I = 0x17, SC_O = 0x18, SC_P = 0x19;
constexpr uint16_t SC_A = 0x1E, SC_S = 0x1F, SC_D = 0x20, SC_F = 0x21, SC_G = 0x22;
constexpr uint16_t SC_H = 0x23, SC_J = 0x24, SC_K = 0x25, SC_L = 0x26;
constexpr uint16_t SC_Z = 0x2C, SC_X = 0x2D, SC_C = 0x2E, SC_V = 0x2F, SC_B = 0x30;
constexpr uint16_t SC_N = 0x31, SC_M = 0x32;

std::mutex g_mapMutex;
std::array<int16_t, 256> g_scanToNote = DefaultWootingScancodeNoteMap();

} // namespace

std::array<int16_t, 256> DefaultWootingScancodeNoteMap() {
    std::array<int16_t, 256> map{};
    map.fill(-1);

    // White keys, C2 upward, in the order the app types them out: bottom row,
    // then home and top rows, then the number row. Black keys are left
    // unmapped until the user's own mapping is wired in; a shifted key is a
    // different key to the analog SDK, and guessing that mapping here would be
    // inventing behaviour.
    const uint16_t whites[] = {
        SC_Z, SC_X, SC_C, SC_V, SC_B, SC_N, SC_M,
        SC_A, SC_S, SC_D, SC_F, SC_G, SC_H, SC_J, SC_K, SC_L,
        SC_Q, SC_W, SC_E, SC_R, SC_T, SC_Y, SC_U, SC_I, SC_O, SC_P,
        SC_1, SC_2, SC_3, SC_4, SC_5, SC_6, SC_7, SC_8, SC_9, SC_0
    };
    // C2 is MIDI note 36. Whites follow the major scale pattern from C.
    const int steps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    int note = 36;
    for (size_t i = 0; i < sizeof(whites) / sizeof(whites[0]); ++i) {
        const int octave = static_cast<int>(i) / 7;
        const int degree = static_cast<int>(i) % 7;
        note = 36 + octave * 12 + steps[degree];
        if (note > 127) break;
        map[whites[i]] = static_cast<int16_t>(note);
    }
    return map;
}

void SetWootingScancodeNoteMap(const std::array<int16_t, 256>& map) {
    std::lock_guard<std::mutex> lock(g_mapMutex);
    g_scanToNote = map;
}

bool WootingAnalogAvailable() {
    Sdk& s = sdk();
    if (!s.bound()) return false;
    if (s.isInitialised()) return true;
    const int devices = s.initialise();
    return devices > 0;
}

namespace {

class WootingAnalogInput final : public IMidiInput {
public:
    ~WootingAnalogInput() override { close(); }

    MidiBackend backend() const noexcept override { return MidiBackend::WootingAnalog; }

    std::vector<MidiInputDevice> enumerate() override {
        if (!WootingAnalogAvailable()) return {};
        return { { L"wooting:analog", L"Wooting analog keys (direct)", MidiBackend::WootingAnalog } };
    }

    bool open(const std::wstring& deviceId, MidiInputCallback callback) override {
        close();
        if (deviceId != L"wooting:analog") return false;

        Sdk& s = sdk();
        if (!s.bound()) return false;
        if (!s.isInitialised() && s.initialise() < 0) return false;
        // Set 1 scancodes are what the rest of this app already speaks.
        s.setKeycodeMode(ScanCode1);

        m_callback = std::move(callback);
        m_openedId = deviceId;
        m_running.store(true, std::memory_order_release);
        m_thread = std::thread(&WootingAnalogInput::pollLoop, this);
        return true;
    }

    void close() override {
        if (m_running.exchange(false, std::memory_order_acq_rel)) {
            if (m_thread.joinable()) m_thread.join();
        }
        m_callback = nullptr;
        m_openedId.clear();
    }

    bool isOpen() const noexcept override { return m_running.load(std::memory_order_acquire); }
    const std::wstring& openedDeviceId() const noexcept override { return m_openedId; }

private:
    // Analog depth at which a key counts as struck, and the lower value it has
    // to come back through before it can be struck again. The gap is what stops
    // a key resting near the threshold from stuttering.
    static constexpr float kOnThreshold = 0.35f;
    static constexpr float kOffThreshold = 0.20f;
    static constexpr size_t kMaxKeys = 32;

    void pollLoop() {
        // A dedicated thread, never the UI thread: injection must not share a
        // thread with a message loop (HANDOFF section 4).
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        Sdk& s = sdk();
        std::array<uint16_t, kMaxKeys> codes{};
        std::array<float, kMaxKeys> values{};
        std::array<float, 256> lastDepth{};
        lastDepth.fill(0.0f);
        std::array<bool, 256> down{};
        down.fill(false);

        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        LARGE_INTEGER previous{};
        QueryPerformanceCounter(&previous);

        while (m_running.load(std::memory_order_acquire)) {
            const int count = s.readFullBuffer(codes.data(), values.data(), static_cast<unsigned>(kMaxKeys));

            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const double dt = static_cast<double>(now.QuadPart - previous.QuadPart) / static_cast<double>(freq.QuadPart);
            previous = now;

            std::array<bool, 256> seen{};
            seen.fill(false);

            if (count > 0) {
                std::lock_guard<std::mutex> lock(g_mapMutex);
                for (int i = 0; i < count && i < static_cast<int>(kMaxKeys); ++i) {
                    const uint16_t code = codes[i];
                    if (code >= 256) continue;
                    const int16_t note = g_scanToNote[code];
                    if (note < 0) continue;

                    seen[code] = true;
                    const float depth = values[i];
                    const float previousDepth = lastDepth[code];
                    lastDepth[code] = depth;

                    if (!down[code] && depth >= kOnThreshold) {
                        down[code] = true;
                        emitNoteOn(static_cast<uint8_t>(note),
                                   velocityFor(depth, previousDepth, dt),
                                   static_cast<uint64_t>(now.QuadPart));
                    }
                    else if (down[code] && depth <= kOffThreshold) {
                        down[code] = false;
                        emitNoteOff(static_cast<uint8_t>(note), static_cast<uint64_t>(now.QuadPart));
                    }
                }
            }

            // A key that drops out of the buffer has been released.
            for (size_t code = 0; code < down.size(); ++code) {
                if (!down[code] || seen[code]) continue;
                down[code] = false;
                lastDepth[code] = 0.0f;
                std::lock_guard<std::mutex> lock(g_mapMutex);
                const int16_t note = g_scanToNote[code];
                if (note >= 0) emitNoteOff(static_cast<uint8_t>(note), static_cast<uint64_t>(now.QuadPart));
            }

            // 1kHz. Fast enough that the poll adds well under a millisecond to
            // the chain, cheap enough to sit beside a game.
            Sleep(1);
        }

        for (size_t code = 0; code < down.size(); ++code) {
            if (!down[code]) continue;
            std::lock_guard<std::mutex> lock(g_mapMutex);
            const int16_t note = g_scanToNote[code];
            if (note >= 0) emitNoteOff(static_cast<uint8_t>(note), 0);
        }
    }

    // How hard the key was struck, from how fast it was travelling as it
    // crossed the threshold. Depth alone cannot work: every key crosses the
    // threshold at the same depth, so depth at that moment is a constant.
    static uint8_t velocityFor(float depth, float previousDepth, double dt) {
        if (dt <= 0.0) return 96;
        const double rate = (static_cast<double>(depth) - static_cast<double>(previousDepth)) / dt;
        // A brisk press covers the travel in about 40ms, so roughly 25 units of
        // depth per second is a firm strike; scale that onto 1..127.
        double scaled = rate / 25.0;
        if (scaled < 0.0) scaled = 0.0;
        if (scaled > 1.0) scaled = 1.0;
        const int velocity = 1 + static_cast<int>(scaled * 126.0);
        return static_cast<uint8_t>(velocity);
    }

    void emitNoteOn(uint8_t note, uint8_t velocity, uint64_t timestampQpc) {
        const uint8_t message[3] = { 0x90, note, velocity };
        if (m_callback) m_callback(timestampQpc, message, sizeof(message));
    }

    void emitNoteOff(uint8_t note, uint64_t timestampQpc) {
        const uint8_t message[3] = { 0x80, note, 0 };
        if (m_callback) m_callback(timestampQpc, message, sizeof(message));
    }

    MidiInputCallback m_callback;
    std::wstring m_openedId;
    std::atomic<bool> m_running{ false };
    std::thread m_thread;
};

} // namespace

std::unique_ptr<IMidiInput> CreateWootingAnalogInput() {
    return std::make_unique<WootingAnalogInput>();
}
