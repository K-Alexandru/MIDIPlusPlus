// KernelStreamingInput.cpp: an IMidiInput that talks to the KS filter directly.
//
// WinRT and WinMM are client APIs. Both end up at the same WDM driver, and both
// add a layer between the driver's callback and ours: WinMM marshals through
// midiInProc on a system thread, WinRT through a WinRT event and an
// IBuffer. Kernel Streaming is the layer they are built on. Opening the pin
// ourselves removes their marshalling from the path, which is the only reason
// to prefer it -- there is no setting here that makes anything faster, just one
// fewer hop.
//
// HANDOFF.md section 10 said no Kernel Streaming backend existed anywhere in
// MIDI++, that KS appeared only in the third-party app's screenshots and in our
// own mockup as a planned option. This is that option, built.
//
// What this deliberately does not have: buffer size and buffer count controls.
// The mockup draws them because the app it was drawn from draws them. A KS MIDI
// capture pin has no ring to size -- KSMUSICFORMAT events are delivered as they
// arrive into whatever buffer the read supplied -- so those two steppers would
// be wired to nothing. Latency here is measured by input_latency, not chosen.
//
// Requires nothing installed. ks.h and ksmedia.h ship in the Windows SDK's
// shared/ directory and ksuser.lib in its um/x64 lib directory, both of which
// the SDK this project already builds against provides.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "MidiInput.hpp"
#include "MidiStreamSplit.hpp"

#include <windows.h>
// WIN32_LEAN_AND_MEAN leaves out winioctl.h, and ks.h builds its IOCTL codes
// out of CTL_CODE, METHOD_NEITHER and FILE_ANY_ACCESS.
#include <winioctl.h>
#include <setupapi.h>

// The KS headers declare their GUIDs and only define them where INITGUID is set
// before the include. This is the one translation unit that names them, so it
// is the one that instantiates them.
#define INITGUID
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ksuser.lib")

// ksuser.dll's pin factory. Declared here rather than pulled in through
// ksproxy.h, which drags in DirectShow's headers for one function.
extern "C" __declspec(dllimport) DWORD __stdcall KsCreatePin(
    HANDLE FilterHandle, PKSPIN_CONNECT Connect, ACCESS_MASK DesiredAccess, PHANDLE ConnectionHandle);

namespace {

// The walk in MidiStreamSplit.hpp mirrors KSMUSICFORMAT so it can be tested
// without the KS headers. If the SDK ever changes that layout, this is where
// it stops compiling rather than where it starts misparsing.
static_assert(sizeof(KSMUSICFORMAT) == sizeof(midi_stream::KsMusicHeader),
              "KSMUSICFORMAT and KsMusicHeader must be the same 8 bytes");
static_assert(offsetof(KSMUSICFORMAT, ByteCount) == offsetof(midi_stream::KsMusicHeader, byteCount),
              "ByteCount must sit at the same offset in both");

constexpr wchar_t kKsPrefix[] = L"ks:";

inline uint64_t nowQpc() {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<uint64_t>(c.QuadPart);
}

// A KS id is "ks:<pin>|<device interface path>". The path is a device
// interface name and can contain almost anything, so the pin index goes first
// and the bar is found from the front.
bool ParseKsId(const std::wstring& id, ULONG& pin, std::wstring& path) {
    const size_t prefix = wcslen(kKsPrefix);
    if (id.size() < prefix || id.compare(0, prefix, kKsPrefix) != 0) return false;
    const size_t bar = id.find(L'|', prefix);
    if (bar == std::wstring::npos) return false;
    const std::wstring digits = id.substr(prefix, bar - prefix);
    if (digits.empty()) return false;
    for (wchar_t c : digits) if (c < L'0' || c > L'9') return false;
    pin = static_cast<ULONG>(_wtoi(digits.c_str()));
    path = id.substr(bar + 1);
    return true;
}

std::wstring MakeKsId(ULONG pin, const std::wstring& path) {
    return kKsPrefix + std::to_wstring(pin) + L"|" + path;
}

// One synchronous KS property request. Every query below is this call with a
// different property id, so the retry-for-size dance lives here once.
bool KsProperty(HANDLE handle, const GUID& set, ULONG id, ULONG flags,
                void* out, ULONG outBytes, ULONG* returned = nullptr) {
    KSPROPERTY property{};
    property.Set = set;
    property.Id = id;
    property.Flags = flags;
    DWORD bytes = 0;
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) return false;
    BOOL ok = DeviceIoControl(handle, IOCTL_KS_PROPERTY, &property, sizeof(property),
                              out, outBytes, &bytes, &overlapped);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(handle, &overlapped, &bytes, TRUE);
    CloseHandle(overlapped.hEvent);
    if (returned) *returned = bytes;
    return ok != FALSE;
}

// Closes a handle however the caller leaves.
struct ScopedHandle {
    HANDLE value;
    ~ScopedHandle() { if (value) CloseHandle(value); }
};

// One overlapped pin-property call, with the OVERLAPPED and its event reset
// first.
//
// This is not tidying. Both queries below are a size probe followed by a
// fetch, and both used to run the second call on the OVERLAPPED the first had
// finished with. A manual-reset event left signalled makes the next
// GetOverlappedResult return at once, reporting the operation that already
// completed, before the driver has written anything into the new buffer. The
// buffer is value-initialised, so a pin whose fetch was answered that way
// reads as having no data ranges at all, and a real MIDI pin quietly does not
// appear in the device list. It depends on how fast the driver answers, so it
// is a device-and-machine lottery rather than something that shows up here.
bool PinProperty(HANDLE filter, KSP_PIN& request, void* out, ULONG outBytes,
                 DWORD& returned, HANDLE event) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    ResetEvent(event);
    returned = 0;
    BOOL ok = DeviceIoControl(filter, IOCTL_KS_PROPERTY, &request, sizeof(request),
                              out, outBytes, &returned, &overlapped);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(filter, &overlapped, &returned, TRUE);
    return ok != FALSE;
}

// A pin carries MIDI if any of its data ranges says music/MIDI. Both the plain
// subtype and the MIDI-with-timecode variant count: the timecode form is what
// several class drivers advertise, and the payload framing is identical.
bool PinCarriesMidi(HANDLE filter, ULONG pin) {
    KSP_PIN request{};
    request.Property.Set = KSPROPSETID_Pin;
    request.Property.Id = KSPROPERTY_PIN_DATARANGES;
    request.Property.Flags = KSPROPERTY_TYPE_GET;
    request.PinId = pin;

    ScopedHandle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event.value) return false;

    // The probe is expected to fail with the size in hand, so its result is
    // not the question; the size is.
    DWORD size = 0;
    PinProperty(filter, request, nullptr, 0, size, event.value);
    if (size < sizeof(KSMULTIPLE_ITEM) || size > (1u << 20)) return false;

    std::vector<uint8_t> blob(size);
    DWORD bytes = 0;
    if (!PinProperty(filter, request, blob.data(), size, bytes, event.value)) return false;
    if (bytes < sizeof(KSMULTIPLE_ITEM)) return false;

    const auto* items = reinterpret_cast<const KSMULTIPLE_ITEM*>(blob.data());
    const uint8_t* cursor = blob.data() + sizeof(KSMULTIPLE_ITEM);
    const uint8_t* end = blob.data() + (bytes < items->Size ? bytes : items->Size);
    for (ULONG i = 0; i < items->Count; ++i) {
        if (cursor + sizeof(KSDATARANGE) > end) break;
        const auto* range = reinterpret_cast<const KSDATARANGE*>(cursor);
        if (range->FormatSize < sizeof(KSDATARANGE) || cursor + range->FormatSize > end) break;
        if (IsEqualGUID(range->MajorFormat, KSDATAFORMAT_TYPE_MUSIC) &&
            (IsEqualGUID(range->SubFormat, KSDATAFORMAT_SUBTYPE_MIDI) ||
             IsEqualGUID(range->SubFormat, KSDATAFORMAT_SUBTYPE_MIDI_BUS)))
            return true;
        // Ranges are packed on an 8-byte boundary.
        cursor += (range->FormatSize + 7) & ~7u;
    }
    return false;
}

bool PinIsCapture(HANDLE filter, ULONG pin) {
    KSP_PIN request{};
    request.Property.Set = KSPROPSETID_Pin;
    request.Property.Flags = KSPROPERTY_TYPE_GET;
    request.PinId = pin;

    const auto query = [&](ULONG id, ULONG& value) {
        request.Property.Id = id;
        DWORD bytes = 0;
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) return false;
        BOOL ok = DeviceIoControl(filter, IOCTL_KS_PROPERTY, &request, sizeof(request),
                                  &value, sizeof(value), &bytes, &overlapped);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
            ok = GetOverlappedResult(filter, &overlapped, &bytes, TRUE);
        CloseHandle(overlapped.hEvent);
        return ok != FALSE;
    };

    ULONG dataflow = 0, communication = 0;
    if (!query(KSPROPERTY_PIN_DATAFLOW, dataflow)) return false;
    if (!query(KSPROPERTY_PIN_COMMUNICATION, communication)) return false;
    // Data flows out of the device towards us, and the pin must be one we are
    // allowed to instantiate rather than one the graph wires up itself.
    return dataflow == KSPIN_DATAFLOW_OUT &&
           (communication == KSPIN_COMMUNICATION_SINK || communication == KSPIN_COMMUNICATION_BOTH);
}

std::wstring PinName(HANDLE filter, ULONG pin, const std::wstring& fallback) {
    KSP_PIN request{};
    request.Property.Set = KSPROPSETID_Pin;
    request.Property.Id = KSPROPERTY_PIN_NAME;
    request.Property.Flags = KSPROPERTY_TYPE_GET;
    request.PinId = pin;

    ScopedHandle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event.value) return fallback;

    DWORD size = 0;
    PinProperty(filter, request, nullptr, 0, size, event.value);
    std::wstring name;
    if (size >= sizeof(wchar_t) && size < 4096) {
        // One wchar_t of slack, always zero, so a driver that does not
        // terminate its own string cannot be read past the end of the buffer.
        std::vector<uint8_t> blob(size + sizeof(wchar_t), 0);
        DWORD bytes = 0;
        if (PinProperty(filter, request, blob.data(), size, bytes, event.value))
            name = reinterpret_cast<const wchar_t*>(blob.data());
    }
    return name.empty() ? fallback : name;
}

std::wstring FriendlyName(HDEVINFO set, SP_DEVICE_INTERFACE_DATA& interfaceData) {
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, nullptr, 0, &needed, &info);
    if (needed == 0) return {};
    std::vector<uint8_t> buffer(needed);
    auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, detail, needed, nullptr, &info)) return {};

    wchar_t name[256]{};
    if (SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_FRIENDLYNAME, nullptr,
                                          reinterpret_cast<PBYTE>(name), sizeof(name), nullptr) ||
        SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_DEVICEDESC, nullptr,
                                          reinterpret_cast<PBYTE>(name), sizeof(name), nullptr))
        return name;
    return {};
}

// The device interface path, which is what CreateFile opens.
std::wstring InterfacePath(HDEVINFO set, SP_DEVICE_INTERFACE_DATA& interfaceData) {
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, nullptr, 0, &needed, nullptr);
    if (needed == 0) return {};
    std::vector<uint8_t> buffer(needed);
    auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, detail, needed, nullptr, nullptr)) return {};
    return detail->DevicePath;
}

HANDLE OpenFilter(const std::wstring& path) {
    return CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
}

class KernelStreamingMidiInput final : public IMidiInput {
public:
    ~KernelStreamingMidiInput() override { close(); }

    MidiBackend backend() const noexcept override { return MidiBackend::KernelStreaming; }

    std::vector<MidiInputDevice> enumerate() override {
        std::vector<MidiInputDevice> out;
        HDEVINFO set = SetupDiGetClassDevsW(&KSCATEGORY_CAPTURE, nullptr, nullptr,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return out;

        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &KSCATEGORY_CAPTURE, index, &interfaceData); ++index) {
            const std::wstring path = InterfacePath(set, interfaceData);
            if (path.empty()) continue;
            const std::wstring friendly = FriendlyName(set, interfaceData);

            HANDLE filter = OpenFilter(path);
            if (filter == INVALID_HANDLE_VALUE) continue;

            ULONG pins = 0;
            if (KsProperty(filter, KSPROPSETID_Pin, KSPROPERTY_PIN_CTYPES,
                           KSPROPERTY_TYPE_GET, &pins, sizeof(pins))) {
                for (ULONG pin = 0; pin < pins; ++pin) {
                    if (!PinIsCapture(filter, pin) || !PinCarriesMidi(filter, pin)) continue;
                    const std::wstring shown = PinName(filter, pin, friendly.empty() ? L"MIDI pin" : friendly);
                    out.push_back({MakeKsId(pin, path), shown, MidiBackend::KernelStreaming});
                }
            }
            CloseHandle(filter);
        }
        SetupDiDestroyDeviceInfoList(set);
        return out;
    }

    bool open(const std::wstring& deviceId, MidiInputCallback callback) override {
        close();
        ULONG pin = 0;
        std::wstring path;
        // An id that names a device is opened or nothing is: never a different
        // pin because this one is gone. Same rule as the other two backends.
        if (!ParseKsId(deviceId, pin, path)) return false;

        filter_ = OpenFilter(path);
        if (filter_ == INVALID_HANDLE_VALUE) { filter_ = nullptr; return false; }

        struct Connect {
            KSPIN_CONNECT connect;
            KSDATAFORMAT format;
        } request{};
        request.connect.Interface.Set = KSINTERFACESETID_Standard;
        request.connect.Interface.Id = KSINTERFACE_STANDARD_STREAMING;
        request.connect.Medium.Set = KSMEDIUMSETID_Standard;
        request.connect.Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
        request.connect.PinId = pin;
        request.connect.PinToHandle = nullptr;
        request.connect.Priority.PriorityClass = KSPRIORITY_NORMAL;
        request.connect.Priority.PrioritySubClass = 1;
        request.format.FormatSize = sizeof(KSDATAFORMAT);
        request.format.MajorFormat = KSDATAFORMAT_TYPE_MUSIC;
        request.format.SubFormat = KSDATAFORMAT_SUBTYPE_MIDI;
        request.format.Specifier = KSDATAFORMAT_SPECIFIER_NONE;

        HANDLE pinHandle = nullptr;
        if (KsCreatePin(filter_, &request.connect, GENERIC_READ, &pinHandle) != ERROR_SUCCESS || !pinHandle) {
            closeHandles();
            return false;
        }
        pin_ = pinHandle;

        // ACQUIRE then PAUSE then RUN. Drivers are entitled to reject a jump
        // straight to RUN, and several do.
        if (!setState(KSSTATE_ACQUIRE) || !setState(KSSTATE_PAUSE) || !setState(KSSTATE_RUN)) {
            closeHandles();
            return false;
        }

        callback_ = std::move(callback);
        openedId_ = deviceId;
        stop_.store(false, std::memory_order_release);
        reader_ = std::thread([this] { readLoop(); });
        return true;
    }

    void close() override {
        if (reader_.joinable()) {
            stop_.store(true, std::memory_order_release);
            // The read is overlapped and parked in GetOverlappedResult, so it
            // has to be cancelled rather than waited out: a pin with no traffic
            // never completes on its own.
            if (pin_) CancelIoEx(pin_, nullptr);
            reader_.join();
        }
        if (pin_) { setState(KSSTATE_PAUSE); setState(KSSTATE_STOP); }
        closeHandles();
        callback_ = nullptr;
        openedId_.clear();
    }

    bool isOpen() const noexcept override { return pin_ != nullptr; }
    const std::wstring& openedDeviceId() const noexcept override { return openedId_; }

private:
    void closeHandles() {
        if (pin_) { CloseHandle(pin_); pin_ = nullptr; }
        if (filter_) { CloseHandle(filter_); filter_ = nullptr; }
    }

    bool setState(KSSTATE state) {
        if (!pin_) return false;
        KSPROPERTY property{};
        property.Set = KSPROPSETID_Connection;
        property.Id = KSPROPERTY_CONNECTION_STATE;
        property.Flags = KSPROPERTY_TYPE_SET;
        DWORD bytes = 0;
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) return false;
        BOOL ok = DeviceIoControl(pin_, IOCTL_KS_PROPERTY, &property, sizeof(property),
                                  &state, sizeof(state), &bytes, &overlapped);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
            ok = GetOverlappedResult(pin_, &overlapped, &bytes, TRUE);
        CloseHandle(overlapped.hEvent);
        return ok != FALSE;
    }

    void readLoop() {
        // One read at a time. A MIDI pin delivers events, not a stream that has
        // to be kept fed, so a second outstanding read would buy nothing and
        // cost the ordering guarantee this loop gets for free.
        alignas(8) uint8_t buffer[4096];
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) return;
        midi_stream::Splitter splitter;

        while (!stop_.load(std::memory_order_acquire)) {
            KSSTREAM_HEADER header{};
            header.Size = sizeof(header);
            header.PresentationTime.Numerator = 1;
            header.PresentationTime.Denominator = 1;
            header.FrameExtent = sizeof(buffer);
            header.Data = buffer;

            // Cleared before every read, not after.
            //
            // The buffer outlives the read and a read overwrites only the bytes
            // it produced, so without this everything past DataUsed is still
            // the previous read's events. Any driver that reports DataUsed
            // even slightly high then replays them, and on 2026-09-06 a tester
            // played one note and heard about eighteen: the whole buffer's
            // worth of history. Zeroed, a stale event reads as a zero
            // ByteCount, which ends the walk.
            //
            // It is a 4KB memset against a device read. Nothing measurable.
            std::memset(buffer, 0, sizeof(buffer));

            OVERLAPPED overlapped{};
            overlapped.hEvent = event;
            ResetEvent(event);
            DWORD bytes = 0;
            BOOL ok = DeviceIoControl(pin_, IOCTL_KS_READ_STREAM, nullptr, 0,
                                      &header, sizeof(header), &bytes, &overlapped);
            if (!ok && GetLastError() == ERROR_IO_PENDING)
                ok = GetOverlappedResult(pin_, &overlapped, &bytes, TRUE);
            if (stop_.load(std::memory_order_acquire)) break;
            if (!ok) {
                // A cancelled read is the ordinary way close() ends this loop
                // and is silent. Anything else means the device went away, and
                // both used to break without a word, so live input stopped and
                // the app said nothing at all. Both branches still break,
                // because there is nothing to recover to, but the second is a
                // failure and is now reported as one.
                const DWORD error = GetLastError();
                if (error != ERROR_OPERATION_ABORTED)
                    std::wcerr << L"Kernel Streaming read failed, live input has stopped. Error "
                               << error << std::endl;
                break;
            }

            // t0 of the latency chain, taken here rather than after parsing so
            // it means the same thing as the other two backends' timestamps.
            const uint64_t timestamp = nowQpc();
            // DataUsed is the driver's word for how much it wrote, and it is
            // the only thing bounding the walk, so it is not taken on trust:
            // a value past the end of our own buffer is the driver being wrong
            // about our memory.
            const ULONG used = (std::min)(header.DataUsed, static_cast<ULONG>(sizeof(buffer)));
            deliver(buffer, used, timestamp, splitter);
        }
        CloseHandle(event);
    }

    // The payload is a run of KSMUSICFORMAT headers, each followed by its bytes
    // padded up to a 4-byte boundary. TimeDeltaMs is the driver's own spacing
    // and is not used: we timestamp on arrival, and mixing the two clocks would
    // make the latency figures mean nothing.
    //
    // The walk itself is midi_stream::FeedKsEvents, where it can be driven with
    // a buffer that still holds an earlier read's events. That case is what
    // this backend got wrong, and it cannot be asked of a real driver.
    void deliver(const uint8_t* data, ULONG used, uint64_t timestamp, midi_stream::Splitter& splitter) {
        if (!callback_) return;
        midi_stream::FeedKsEvents(splitter, data, used,
            [&](const uint8_t* message, size_t length) {
                if (callback_) callback_(timestamp, message, length);
            });
    }

    HANDLE filter_ = nullptr;
    HANDLE pin_ = nullptr;
    std::thread reader_;
    std::atomic<bool> stop_{false};
    MidiInputCallback callback_;
    std::wstring openedId_;
};

} // namespace

std::unique_ptr<IMidiInput> CreateKernelStreamingInput() {
    return std::make_unique<KernelStreamingMidiInput>();
}

bool KernelStreamingIdentifies(const std::wstring& deviceId) {
    const size_t prefix = wcslen(kKsPrefix);
    return deviceId.size() >= prefix && deviceId.compare(0, prefix, kKsPrefix) == 0;
}
