// MidiInput.cpp: WinRT and WinMM implementations of IMidiInput.

#include "MidiInput.hpp"

#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Midi.h>
#include <winrt/Windows.Storage.Streams.h>

#include "RtMidi.h"
#include "WootingAnalog.hpp"

#include <mutex>
#include <string>

namespace {

// WinMM ids are prefixed so BackendForDeviceId can route without a lookup.
constexpr wchar_t kWinMMPrefix[] = L"winmm:";
constexpr wchar_t kWootingPrefix[] = L"wooting:";

inline uint64_t nowQpc() {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<uint64_t>(c.QuadPart);
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

// The UI thread may already be an STA (shell dialogs, drag-drop and OLE all do
// this), in which case asking for an MTA throws RPC_E_CHANGED_MODE. Either
// apartment works for us, so treat that as success.
void ensureApartment() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (winrt::hresult_error const& ex) {
        if (ex.code() != RPC_E_CHANGED_MODE) throw;
    }
}

class WinRTMidiInput final : public IMidiInput {
public:
    ~WinRTMidiInput() override { close(); }

    MidiBackend backend() const noexcept override { return MidiBackend::WinRT; }

    std::vector<MidiInputDevice> enumerate() override {
        std::vector<MidiInputDevice> out;
        try {
            ensureApartment();
            auto selector = winrt::Windows::Devices::Midi::MidiInPort::GetDeviceSelector();
            auto devices = winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(selector).get();
            out.reserve(devices.Size());
            for (auto const& info : devices) {
                out.push_back({ std::wstring(info.Id()), std::wstring(info.Name()), MidiBackend::WinRT });
            }
        }
        catch (winrt::hresult_error const&) {
            out.clear();
        }
        return out;
    }

    bool open(const std::wstring& deviceId, MidiInputCallback callback) override {
        close();
        if (deviceId.empty()) return false;
        try {
            ensureApartment();
            m_port = winrt::Windows::Devices::Midi::MidiInPort::FromIdAsync(winrt::hstring(deviceId)).get();
        }
        catch (winrt::hresult_error const&) {
            m_port = nullptr;
        }
        if (!m_port) return false;

        m_callback = std::move(callback);
        m_token = m_port.MessageReceived(
            [this](winrt::Windows::Devices::Midi::MidiInPort const&,
                   winrt::Windows::Devices::Midi::MidiMessageReceivedEventArgs const& args) {
                const uint64_t t0 = nowQpc();
                auto raw = args.Message().RawData();
                if (!raw || raw.Length() == 0 || !m_callback) return;
                m_callback(t0, raw.data(), static_cast<size_t>(raw.Length()));
            });
        m_openedId = deviceId;
        return true;
    }

    void close() override {
        if (m_port) {
            m_port.MessageReceived(m_token);
            m_port.Close();
            m_port = nullptr;
        }
        m_callback = nullptr;
        m_openedId.clear();
    }

    bool isOpen() const noexcept override { return static_cast<bool>(m_port); }
    const std::wstring& openedDeviceId() const noexcept override { return m_openedId; }

private:
    winrt::Windows::Devices::Midi::MidiInPort m_port{ nullptr };
    winrt::event_token m_token{};
    MidiInputCallback m_callback;
    std::wstring m_openedId;
};

class WinMMMidiInput final : public IMidiInput {
public:
    ~WinMMMidiInput() override { close(); }

    MidiBackend backend() const noexcept override { return MidiBackend::WinMM; }

    std::vector<MidiInputDevice> enumerate() override {
        std::vector<MidiInputDevice> out;
        try {
            RtMidiIn probe(RtMidi::Api::WINDOWS_MM, "MIDI++ enumerate", 100);
            const unsigned count = probe.getPortCount();
            out.reserve(count);
            for (unsigned i = 0; i < count; ++i) {
                std::wstring name = widen(probe.getPortName(i));
                // The port name is the identity; the index only breaks the tie
                // when two ports report the same name.
                out.push_back({ kWinMMPrefix + std::to_wstring(i) + L"|" + name, name, MidiBackend::WinMM });
            }
        }
        catch (RtMidiError const&) {
            out.clear();
        }
        return out;
    }

    bool open(const std::wstring& deviceId, MidiInputCallback callback) override {
        close();
        if (deviceId.empty()) return false;

        unsigned index = 0;
        std::wstring wantedName;
        if (!parseId(deviceId, index, wantedName)) return false;

        try {
            m_in = new RtMidiIn(RtMidi::Api::WINDOWS_MM, "MIDI++", 100);
            const unsigned count = m_in->getPortCount();
            if (count == 0) { destroy(); return false; }

            // Prefer the port whose name matches; fall back to the recorded
            // index. Windows renumbers ports when devices come and go, so the
            // name is the more reliable half of the id.
            unsigned target = count;
            if (!wantedName.empty()) {
                for (unsigned i = 0; i < count; ++i) {
                    if (widen(m_in->getPortName(i)) == wantedName) { target = i; break; }
                }
            }
            if (target >= count) target = (index < count) ? index : 0;

            m_callback = std::move(callback);
            m_in->setCallback(&WinMMMidiInput::trampoline, this);
            m_in->ignoreTypes(true, true, true);
            m_in->setBufferSize(256, 1);
            m_in->openPort(target);
            m_openedId = deviceId;
            return true;
        }
        catch (RtMidiError const&) {
            destroy();
            return false;
        }
    }

    void close() override {
        if (m_in) {
            try {
                m_in->cancelCallback();
                m_in->closePort();
            }
            catch (RtMidiError const&) {
            }
            destroy();
        }
        m_callback = nullptr;
        m_openedId.clear();
    }

    bool isOpen() const noexcept override { return m_in != nullptr; }
    const std::wstring& openedDeviceId() const noexcept override { return m_openedId; }

private:
    static void __stdcall trampoline(double, std::vector<unsigned char>* message, void* user) {
        const uint64_t t0 = nowQpc();
        auto* self = static_cast<WinMMMidiInput*>(user);
        if (!self || !message || message->empty() || !self->m_callback) return;
        self->m_callback(t0, message->data(), message->size());
    }

    static bool parseId(const std::wstring& id, unsigned& index, std::wstring& name) {
        const size_t prefix = wcslen(kWinMMPrefix);
        if (id.compare(0, prefix, kWinMMPrefix) != 0) return false;
        const size_t bar = id.find(L'|', prefix);
        const std::wstring digits = id.substr(prefix, bar == std::wstring::npos ? std::wstring::npos : bar - prefix);
        index = digits.empty() ? 0u : static_cast<unsigned>(_wtoi(digits.c_str()));
        name = (bar == std::wstring::npos) ? std::wstring() : id.substr(bar + 1);
        return true;
    }

    void destroy() {
        delete m_in;
        m_in = nullptr;
    }

    RtMidiIn* m_in = nullptr;
    MidiInputCallback m_callback;
    std::wstring m_openedId;
};

} // namespace

std::unique_ptr<IMidiInput> CreateMidiInput(MidiBackend backend) {
    if (backend == MidiBackend::WootingAnalog) return CreateWootingAnalogInput();
    if (backend == MidiBackend::WinMM) return std::make_unique<WinMMMidiInput>();
    return std::make_unique<WinRTMidiInput>();
}

std::vector<MidiInputDevice> EnumerateMidiInputs() {
    WinRTMidiInput winrtInput;
    auto devices = winrtInput.enumerate();
    if (devices.empty()) {
        WinMMMidiInput winmmInput;
        devices = winmmInput.enumerate();
    }

    // The Wooting keyboard is a real input source, not a MIDI port, so it is
    // listed alongside the ports rather than instead of them. Picking it skips
    // the separate bridge app and the virtual MIDI driver entirely.
    if (WootingAnalogAvailable()) {
        auto wooting = CreateWootingAnalogInput();
        for (auto& device : wooting->enumerate()) devices.push_back(std::move(device));
    }
    return devices;
}

MidiBackend BackendForDeviceId(const std::wstring& deviceId) {
    const size_t winmmPrefix = wcslen(kWinMMPrefix);
    if (deviceId.size() >= winmmPrefix && deviceId.compare(0, winmmPrefix, kWinMMPrefix) == 0) {
        return MidiBackend::WinMM;
    }
    const size_t wootingPrefix = wcslen(kWootingPrefix);
    if (deviceId.size() >= wootingPrefix && deviceId.compare(0, wootingPrefix, kWootingPrefix) == 0) {
        return MidiBackend::WootingAnalog;
    }
    return MidiBackend::WinRT;
}
