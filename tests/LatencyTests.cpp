#include "InputLatency.hpp"
#include "MIDI2Key.hpp"
#include "MIDIConnect.hpp"
#include "InputLatencyWindow.hpp"
#include <gdiplus.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace input_latency;
using namespace std::chrono_literals;
VirtualPianoPlayer* g_player = nullptr;
int g_sustainCutoff = 64;
// Headless test of the real engine, without the decorative splash window.
void ShowSplashScreen(HINSTANCE) {}
void CloseSplashScreen() {}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void ringTests() {
    Ring<uint64_t, 4> tiny;
    for (uint64_t i = 1; i <= 4; ++i) require(tiny.push(i), "ring fill");
    require(!tiny.push(5) && tiny.dropped() == 1, "ring full must drop telemetry");
    for (uint64_t i = 1, value = 0; i <= 4; ++i) require(tiny.pop(value) && value == i, "ring FIFO");
    for (uint64_t i = 0, value = 0; i < 10000; ++i)
        require(tiny.push(i) && tiny.pop(value) && value == i, "ring wrap/reuse");

    struct Payload { uint32_t producer, sequence; uint64_t checksum; };
    Ring<Payload, 1024> ring;
    constexpr size_t Producers = 6, Attempts = 20000;
    std::atomic<size_t> finished{0}, accepted{0};
    std::array<std::thread, Producers> threads;
    for (uint32_t p = 0; p < Producers; ++p) threads[p] = std::thread([&, p] {
        for (uint32_t seq = 1; seq <= Attempts; ++seq) {
            Payload value{p, seq, (static_cast<uint64_t>(p) << 32) | seq};
            if (ring.push(value)) ++accepted;
        }
        finished.fetch_add(1, std::memory_order_release);
    });
    std::array<uint32_t, Producers> last{};
    size_t consumed = 0;
    const auto drain = [&] {
        Payload value{};
        while (ring.pop(value)) {
            require(value.producer < Producers, "torn producer");
            require(value.sequence > last[value.producer], "per-producer ordering");
            require(value.checksum == ((static_cast<uint64_t>(value.producer) << 32) | value.sequence), "torn payload");
            last[value.producer] = value.sequence;
            ++consumed;
        }
    };
    while (finished.load(std::memory_order_acquire) < Producers) { drain(); std::this_thread::yield(); }
    for (auto& thread : threads) thread.join();
    drain();
    require(consumed == accepted && consumed + ring.dropped() == Producers * Attempts, "ring accounting");
    std::cout << "PASS ring saturation, wrap and 120000 concurrent attempts\n";
}

Record observed(uint64_t id, uint64_t time) {
    Record record;
    record.type = RecordType::Observation;
    record.submission.id = id;
    record.submission.t2 = time;
    return record;
}

void collectorTests() {
    Collector collector;
    Record record;
    record.submission = {1, 1000, 1100, 1400, 250, 2, 2, 2, 0, 0, 0, Source::LiveKeys, Kind::NoteOn};
    collector.ingest(observed(1, 1200), 1400);
    collector.ingest(record, 1400);
    collector.ingest(observed(1, 1300), 1400);
    auto summary = collector.summarize(Source::LiveKeys, 1000000);
    require(summary.notes == 1 && summary.eventsPerNote == 2, "note/event accounting");
    require(summary.callbackToHookMs.p50 == .3 && summary.hookMinusReturnMs.p50 == -.1, "signed hook timing");
    require(summary.callsMs.p50 == .25, "sum of injection calls");
    record.submission.id = 2;
    collector.ingest(record, 2000);
    collector.expire(5000, 2000);
    summary = collector.summarize(Source::LiveKeys, 1000000);
    require(summary.incomplete == 1 && summary.callbackToHookMs.count == 1, "missing hook excluded from timings");
    record.submission.id = 3;
    record.submission.accepted = 0;
    record.submission.failures = 1;
    record.submission.error = 5;
    collector.ingest(record, 5000);
    summary = collector.summarize(Source::LiveKeys, 1000000);
    require(summary.failures == 1 && summary.lastError == 5, "failure reporting");
    record.submission.id = 4;
    record.submission.source = Source::MidiConnect;
    record.submission.kind = Kind::NoteOff;
    collector.ingest(record, 5000);
    require(collector.summarize(Source::MidiConnect, 1000000).notes == 0, "note-offs are a separate population");
    std::vector<double> values;
    for (int i = 1; i <= 100; ++i) values.push_back(i);
    auto p = percentiles(values);
    require(p.p50 == 50 && p.p95 == 95 && p.p99 == 99, "nearest-rank percentiles");
    std::cout << "PASS out-of-order joins, signed timing, loss, failures and percentiles\n";
}

// Installed before the production measurement hook. Only events bearing this
// test process's tags are swallowed; no test note reaches the focused app.
Ring<KBDLLHOOKSTRUCT, 2048> captured;
LRESULT CALLBACK sinkHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if ((event.flags & LLKHF_INJECTED) && isOurTag(event.dwExtraInfo)) {
            captured.push(event);
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

class TestSink {
    std::thread thread_;
    DWORD id_ = 0;
    bool installed_ = false;
public:
    TestSink() {
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        require(ready != nullptr, "sink ready event");
        thread_ = std::thread([&, ready] {
            MSG message{};
            PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            id_ = GetCurrentThreadId();
            HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, sinkHook, GetModuleHandleW(nullptr), 0);
            installed_ = hook != nullptr;
            SetEvent(ready);
            if (!hook) return;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
            UnhookWindowsHookEx(hook);
        });
        WaitForSingleObject(ready, INFINITE);
        CloseHandle(ready);
        if (!installed_) { thread_.join(); throw std::runtime_error("sink hook installation failed; no input sent"); }
    }
    ~TestSink() {
        stop();
        PostThreadMessageW(id_, WM_QUIT, 0, 0);
        thread_.join();
    }
};

std::vector<KBDLLHOOKSTRUCT> takeCaptured() {
    std::vector<KBDLLHOOKSTRUCT> events;
    KBDLLHOOKSTRUCT event{};
    while (captured.pop(event)) events.push_back(event);
    require(captured.dropped() == 0, "test capture overflow");
    return events;
}

UINT fakeResult = 0;
ULONG_PTR lastTag = 0;
UINT __fastcall fakeInjection(ULONG count, LPINPUT inputs, int) {
    lastTag = count && inputs ? inputs[0].ki.dwExtraInfo : 0;
    SetLastError(ERROR_ACCESS_DENIED);
    return fakeResult;
}

// The inherited default returned 69 without injecting, so a build where the
// syscall could not be assembled silently no-opped every keystroke and only the
// traced path noticed. Whichever path is chosen, it must be a real one.
void injectionPathTests() {
    INPUT none[1]{};
    const auto beforeInit = NtUserSendInputCall;
    require(beforeInit != nullptr, "an injection path exists before initialization");
    require(beforeInit(0, none, sizeof(INPUT)) == 0, "empty injection reports nothing sent");

    require(EnsureInputInjection() == UsingSyscallInjection(), "reported path matches the active one");
    require(NtUserSendInputCall != nullptr, "an injection path exists after initialization");
    require(NtUserSendInputCall(0, none, sizeof(INPUT)) == 0, "empty injection still reports nothing sent");

    // Repeat calls must not swap an established path for a second stub.
    const auto established = NtUserSendInputCall;
    require(EnsureInputInjection() == UsingSyscallInjection(), "repeat initialization agrees with itself");
    require(NtUserSendInputCall == established, "repeat initialization keeps the established path");

    std::cout << "PASS injection path never installs the no-op stub and falls back to SendInput\n";
}

void wrapperTests() {
    TestSink sink;
    require(start(), "measurement hook start");
    const auto original = NtUserSendInputCall;
    struct Restore { decltype(NtUserSendInputCall) original; ~Restore() { NtUserSendInputCall = original; } } restore{original};
    NtUserSendInputCall = fakeInjection;
    INPUT inputs[2]{};
    for (auto& input : inputs) { input.type = INPUT_KEYBOARD; input.ki.wScan = 0x14; input.ki.dwFlags = KEYEVENTF_SCANCODE; }
    Collector collector;
    fakeResult = 69;
    { Trace trace(Source::LiveKeys, Kind::NoteOn); send(2, inputs); }
    require(isOurTag(lastTag) && inputs[0].ki.dwExtraInfo == 0, "tags use a local copy");
    poll(collector);
    require(collector.samples(Source::LiveKeys).back().submission.error == ERROR_INVALID_DATA, "impossible injection count");
    fakeResult = 1;
    { Trace trace(Source::LiveKeys, Kind::NoteOn); send(2, inputs); }
    poll(collector);
    collector.expire(nowQpc() + frequency() * 3, frequency() * 2);
    const auto summary = collector.summarize(Source::LiveKeys, frequency());
    require(summary.accepted == 1 && summary.failures == 2 && summary.callbackToHookMs.count == 0, "partial injection must not claim delivery");
    stop();
    { Trace trace(Source::LiveKeys, Kind::NoteOn); send(2, inputs); }
    require(lastTag == 0, "disabled measurement does not tag input");
    std::cout << "PASS injection failure, partial result, shared INPUT preservation and disabled path\n";
}

void saveWindow(HWND window) {
    RECT rect{};
    GetWindowRect(window, &rect);
    HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, rect.right - rect.left, rect.bottom - rect.top);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    require(PrintWindow(window, memory, 0), "render timing window");
    ULONG_PTR token{};
    Gdiplus::GdiplusStartupInput startup;
    require(Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok, "screenshot encoder");
    {
        Gdiplus::Bitmap output(bitmap, nullptr);
        CLSID png{};
        CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
        require(output.Save(L"timing-window.png", &png, nullptr) == Gdiplus::Ok, "save timing screenshot");
    }
    Gdiplus::GdiplusShutdown(token);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, screen);
}

void uiTests() {
    SyscallNumber = GetNtUserSendInputSyscallNumber();
    InitializeNtUserSendInputCall();
    TestSink sink;
    ShowInputLatencyWindow(nullptr);
    HWND window = FindWindowW(L"MIDI++ Input Timing", nullptr);
    require(window && enabled(), "timing window starts measurement");
    std::thread inject([] {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = 0x14;
        for (int i = 0; i < 3; ++i) {
            { Trace trace(Source::LiveKeys, Kind::NoteOn); input.ki.dwFlags = KEYEVENTF_SCANCODE; send(1, &input); }
            { Trace trace(Source::LiveKeys, Kind::NoteOff); input.ki.dwFlags |= KEYEVENTF_KEYUP; send(1, &input); }
        }
    });
    inject.join();
    SendMessageW(window, WM_TIMER, 1, 0);
    wchar_t text[4096]{};
    GetDlgItemTextW(window, 2, text, static_cast<int>(std::size(text)));
    require(std::wstring(text).find(L"3 note-ons") != std::wstring::npos, "timing window displays actual notes");
    saveWindow(window);
    SendDlgItemMessageW(window, 1, CB_SETCURSEL, 2, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(window, 1)));
    GetDlgItemTextW(window, 2, text, static_cast<int>(std::size(text)));
    require(std::wstring(text).find(L"Waiting for notes") != std::wstring::npos, "source picker round trip");
    DestroyWindow(window);
    require(!enabled(), "closing timing window removes hook");
    ShowInputLatencyWindow(nullptr);
    window = FindWindowW(L"MIDI++ Input Timing", nullptr);
    require(window && enabled(), "timing window reopens");
    DestroyWindow(window);
    require(takeCaptured().size() == 6, "UI smoke-test notes were swallowed by test sink");
    std::cout << "PASS rendered timing window, source picker, close and reopen\n";
}

void awaitSamples(Collector& collector, Source source, size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    do {
        poll(collector);
        if (collector.samples(source).size() >= count) return;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);
    std::cerr << "Samples " << collector.samples(source).size() << '/' << count << ", pending " << collector.pending() << '\n';
    for (const auto& sample : collector.samples(source))
        std::cerr << "  kind=" << static_cast<int>(sample.submission.kind) << " requested=" << sample.submission.requested
            << " accepted=" << sample.submission.accepted << " observed=" << sample.observed << '\n';
    throw std::runtime_error("timed out waiting for MIDI/injection samples");
}

void reportSamples(const char* label, const Collector& collector, Source source) {
    auto s = collector.summarize(source, frequency());
    std::cout << label << ": notes=" << s.notes << " events_per_note=" << s.eventsPerNote
        << " hook_ms_p50/p95/p99=" << s.callbackToHookMs.p50 << '/' << s.callbackToHookMs.p95 << '/' << s.callbackToHookMs.p99
        << " failures=" << s.failures << " incomplete=" << s.incomplete << '\n';
    std::ofstream csv(std::string(label) + ".csv");
    csv << "kind,t0,t1,t2,t3,call_ticks,requested,accepted,observed,complete,failures,qpc_frequency\n";
    for (const auto& sample : collector.samples(source)) {
        const auto& row = sample.submission;
        csv << static_cast<int>(row.kind) << ',' << row.t0 << ',' << row.t1 << ',' << row.t2 << ','
            << sample.t3 << ',' << row.callTicks << ',' << row.requested << ',' << row.accepted << ','
            << sample.observed << ',' << sample.complete << ',' << row.failures << ',' << frequency() << '\n';
    }
}

bool matchesPort(const MidiInputDevice& device, const std::wstring& name) {
    if (device.backend != MidiBackend::WinMM) return device.name == name;
    // RtMidi appends the input index to its display name. Resolve using the
    // native device name and the opaque id's recorded index, not output index.
    for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
        MIDIINCAPSW caps{};
        midiInGetDevCapsW(i, &caps, sizeof(caps));
        if (name == caps.szPname && device.id.starts_with(L"winmm:" + std::to_wstring(i) + L"|")) return true;
    }
    return false;
}

template<class Send>
void awaitLoopbackReady(Send midiSend, Source source) {
    // loopMIDI's routes can reconnect asynchronously when switching client APIs.
    // Establish receipt using a harmless pedal-up before the measured fixture.
    Collector readiness;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        midiSend(0xB0, 64, 0);
        const auto retryAt = std::chrono::steady_clock::now() + 50ms;
        do {
            poll(readiness);
            if (!readiness.samples(source).empty()) { takeCaptured(); return; }
            std::this_thread::sleep_for(5ms);
        } while (std::chrono::steady_clock::now() < retryAt);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("loopMIDI route did not become ready");
}

// Legit mode. Needs no MIDI hardware: it drives the real autoplay dispatch path
// with a synthetic score and reads back what actually reached the keyboard hook.
void legitModeTests() {
    std::cout << "Initializing real PlaybackCore for legit mode..." << std::endl;
    VirtualPianoPlayer player;
    g_player = &player;
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    TestSink sink;
    require(start(), "measurement hook start");
    player.enable_velocity_keypress = false;
    auto& cfg = midi::Config::getInstance().legit_mode;

    // Plays a score to completion and returns the injected keyboard events. The
    // trailing wait covers a batch that is still spreading when the last event
    // is consumed.
    const auto run = [&](std::vector<RawNoteEvent> events) {
        const size_t total = events.size();
        player.note_events = std::move(events);
        player.restart_song();
        const auto deadline = std::chrono::steady_clock::now() + 8s;
        while (player.buffer_index.load(std::memory_order_acquire) < total &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(2ms);
        }
        std::this_thread::sleep_for(400ms);
        player.should_stop.store(true, std::memory_order_release);
        SetEvent(player.command_event);
        player.playback_thread->join();
        return takeCaptured();
    };
    const auto downs = [](const std::vector<KBDLLHOOKSTRUCT>& events) {
        size_t n = 0;
        for (const auto& e : events) if (!(e.flags & LLKHF_UP)) ++n;
        return n;
    };
    const auto ups = [](const std::vector<KBDLLHOOKSTRUCT>& events) {
        size_t n = 0;
        for (const auto& e : events) if (e.flags & LLKHF_UP) ++n;
        return n;
    };

    const std::vector<RawNoteEvent> chord = {
        {0ns, "C4", EventType::Press, 70, -1}, {0ns, "E4", EventType::Press, 70, -1},
        {0ns, "G4", EventType::Press, 70, -1},
        {80ms, "C4", EventType::Release, 0, -1}, {80ms, "E4", EventType::Release, 0, -1},
        {80ms, "G4", EventType::Release, 0, -1}};

    // 1. Disabled is the original path: three presses, three releases, nothing else.
    player.legit_mode_active.store(false, std::memory_order_relaxed);
    auto plain = run(chord);
    require(plain.size() == 6 && downs(plain) == 3 && ups(plain) == 3, "legit off leaves dispatch unchanged");

    // 2. Skipping drops presses only, and can never strand a held key. The
    //    orphaned note-off is a no-op because release_key() checks pressed_keys,
    //    which is the bug the 1.0.3 parse-time version had backwards.
    player.legit_seed_override.store(0xA5A5A5A5A5A5A5A5ull, std::memory_order_relaxed);
    cfg.ENABLED = true;
    cfg.TIMING_VARIATION = 0.0;
    cfg.EXTRA_DELAY_CHANCE = 0.0;
    cfg.NOTE_SKIP_CHANCE = 1.0;
    player.legit_mode_active.store(true, std::memory_order_relaxed);
    auto allSkipped = run(chord);
    require(allSkipped.empty(), "every press skipped leaves no injected event at all");

    cfg.NOTE_SKIP_CHANCE = 0.5;
    std::vector<RawNoteEvent> run40;
    for (int i = 0; i < 40; ++i) {
        run40.push_back({std::chrono::milliseconds(i * 12), "C4", EventType::Press, 70, -1});
        run40.push_back({std::chrono::milliseconds(i * 12 + 6), "C4", EventType::Release, 0, -1});
    }
    auto partial = run(run40);
    require(downs(partial) > 0 && downs(partial) < 40, "half the presses were dropped");
    require(downs(partial) == ups(partial), "every surviving press was released; no key left held");

    // 3. Hesitation shifts, it does not stretch. A forced 60ms pause on every
    //    batch must delay each note by the same 60ms rather than accumulating
    //    into the schedule the way the parse-time version did.
    cfg.NOTE_SKIP_CHANCE = 0.0;
    cfg.EXTRA_DELAY_CHANCE = 1.0;
    cfg.EXTRA_DELAY_MIN = 0.06;
    cfg.EXTRA_DELAY_MAX = 0.06;
    std::vector<RawNoteEvent> spaced;
    for (int i = 0; i < 5; ++i) {
        spaced.push_back({std::chrono::milliseconds(i * 200), "C4", EventType::Press, 70, -1});
        spaced.push_back({std::chrono::milliseconds(i * 200 + 100), "C4", EventType::Release, 0, -1});
    }
    auto hesitated = run(spaced);
    require(downs(hesitated) == 5, "hesitation drops nothing");
    DWORD first = 0, last = 0;
    for (const auto& e : hesitated) {
        if (e.flags & LLKHF_UP) continue;
        if (!first) first = e.time;
        last = e.time;
    }
    const long span = static_cast<long>(last - first);
    // True span is 800ms. Accumulating five 60ms pauses would reach ~1040ms.
    std::cout << "legit: skipped=" << (40 - downs(partial)) << "/40 presses, hesitation span="
        << span << "ms (score 800ms, accumulating five pauses would be ~1040ms)\n";
    require(span > 600 && span < 950, "hesitation shifted the score without stretching it");

    cfg.ENABLED = false;
    cfg.TIMING_VARIATION = 0.1;
    cfg.NOTE_SKIP_CHANCE = 0.02;
    cfg.EXTRA_DELAY_CHANCE = 0.05;
    cfg.EXTRA_DELAY_MIN = 0.05;
    cfg.EXTRA_DELAY_MAX = 0.2;
    player.legit_mode_active.store(false, std::memory_order_relaxed);
    player.legit_seed_override.store(0, std::memory_order_relaxed);
    stop();
    g_player = nullptr;
    std::cout << "PASS legit mode: disabled path, skip pairing, no stranded keys, no accumulated drift\n";
}

void loopbackTests(const std::wstring& portName) {
    UINT outputIndex = midiOutGetNumDevs();
    for (UINT i = 0; i < midiOutGetNumDevs(); ++i) {
        MIDIOUTCAPSW caps{};
        midiOutGetDevCapsW(i, &caps, sizeof(caps));
        if (portName == caps.szPname) outputIndex = i;
    }
    require(outputIndex < midiOutGetNumDevs(), "selected loopMIDI output not found");
    HMIDIOUT output = nullptr;
    require(midiOutOpen(&output, outputIndex, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR, "open MIDI output");
    struct CloseOutput { HMIDIOUT value; ~CloseOutput() { midiOutClose(value); } } closeOutput{output};
    const auto midiSend = [&](BYTE status, BYTE note, BYTE velocity) {
        require(midiOutShortMsg(output, status | (note << 8) | (velocity << 16)) == MMSYSERR_NOERROR, "MIDI send");
    };

    std::cout << "Initializing real PlaybackCore (includes inherited TSC calibration)..." << std::endl;
    VirtualPianoPlayer player;
    g_player = &player;
    // Do not let inherited priority tuning turn a test into a realtime process.
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    TestSink sink;
    require(start(), "measurement hook start");
    for (const auto backend : {MidiBackend::WinRT, MidiBackend::WinMM}) {
        auto input = CreateMidiInput(backend);
        auto devices = input->enumerate();
        auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return matchesPort(d, portName); });
        require(found != devices.end(), "loopMIDI input not found in backend");
        const auto id = found->id;
        const char* label = backend == MidiBackend::WinRT ? "winrt-live" : "winmm-live";
        MIDI2Key live(&player);
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        player.enable_velocity_keypress = false;
        player.currentSustainMode = SustainMode::SPACE_DOWN;
        g_sustainCutoff = 80;
        live.SetActive(true);
        live.OpenDevice(id);
        require(live.GetSelectedDevice() == id, "live input opened by id");
        awaitLoopbackReady(midiSend, Source::LiveKeys);
        Collector collector;
        // Triad, normal note-off and MIDI's zero-velocity note-off form.
        for (BYTE note : std::array<BYTE, 3>{60, 64, 67}) midiSend(0x90, note, 70);
        for (BYTE note : std::array<BYTE, 3>{60, 64, 67}) midiSend(note == 64 ? 0x90 : 0x80, note, 0);
        midiSend(0xB0, 64, 60); // below the configured cutoff
        midiSend(0xB0, 64, 90);
        midiSend(0xB0, 64, 0);
        awaitSamples(collector, Source::LiveKeys, 9);
        auto events = takeCaptured();
        require(events.size() == 8, "three notes and sustain must produce eight keyboard events");
        size_t spaceDown = 0, spaceUp = 0, keyDown = 0, keyUp = 0;
        for (const auto& event : events) {
            if (event.scanCode == 0x39) (event.flags & LLKHF_UP ? spaceUp : spaceDown)++;
            else (event.flags & LLKHF_UP ? keyUp : keyDown)++;
        }
        require(spaceDown == 1 && spaceUp == 1 && keyDown == 3 && keyUp == 3, "note/sustain transitions");
        require(!player.isSustainPressed, "sustain released");
        auto summary = collector.summarize(Source::LiveKeys, frequency());
        require(summary.notes == 3 && summary.eventsPerNote == 1 && summary.incomplete == 0, "live notes measured");

        player.enable_velocity_keypress = true;
        for (BYTE velocity : std::array<BYTE, 3>{20, 20, 100}) { midiSend(0x90, 60, velocity); midiSend(0x80, 60, 0); }
        awaitSamples(collector, Source::LiveKeys, 15);
        events = takeCaptured();
        require(events.size() == 14, "two velocity changes add eight events; repeated bucket adds none");
        const auto& samples = collector.samples(Source::LiveKeys);
        require(samples[9].submission.requested == 5 && samples[11].submission.requested == 1 &&
            samples[13].submission.requested == 5, "velocity counts per note-on");
        reportSamples(label, collector, Source::LiveKeys);
        live.SetActive(false);
        live.CloseDevice();
    }

    // Exercise autoplay's real dispatch path using the inherited scheduler.
    player.enable_velocity_keypress = false;
    player.note_events = {{0ns, "C4", EventType::Press, 70, -1}, {40ms, "C4", EventType::Release, 0, -1}};
    Collector autoplay;
    player.restart_song();
    awaitSamples(autoplay, Source::Autoplay, 2);
    player.should_stop.store(true);
    SetEvent(player.command_event);
    player.playback_thread->join();
    require(autoplay.summarize(Source::Autoplay, frequency()).callbackToHookMs.count == 1, "autoplay measured");
    require(takeCaptured().size() == 2, "autoplay note press/release");
    reportSamples("autoplay", autoplay, Source::Autoplay);

    player.enable_velocity_keypress = true;
    player.lastPressedKey.clear();
    player.note_events = {
        {0ns, "C4", EventType::Press, 20, -1}, {20ms, "C4", EventType::Release, 0, -1},
        {40ms, "C4", EventType::Press, 20, -1}, {60ms, "C4", EventType::Release, 0, -1},
        {80ms, "C4", EventType::Press, 100, -1}, {100ms, "C4", EventType::Release, 0, -1}};
    Collector autoplayVelocity;
    player.restart_song();
    awaitSamples(autoplayVelocity, Source::Autoplay, 6);
    player.should_stop.store(true);
    SetEvent(player.command_event);
    player.playback_thread->join();
    const auto& velocitySamples = autoplayVelocity.samples(Source::Autoplay);
    // Autoplay now sends the same four-event ALT tap in one call that MIDI2Key
    // does, so a changed bucket is 5 requested events (4 velocity + 1 press),
    // not the 7 it used to be from two three-event KeyPress calls.
    require(velocitySamples[0].submission.requested == 5 && velocitySamples[2].submission.requested == 1 &&
        velocitySamples[4].submission.requested == 5, "autoplay uses four velocity events per changed bucket");
    require(takeCaptured().size() == 14, "autoplay velocity event total");
    reportSamples("autoplay-velocity", autoplayVelocity, Source::Autoplay);

    // MIDIConnect has a different ten-event receiver protocol, tracked separately.
    auto winmm = CreateMidiInput(MidiBackend::WinMM);
    auto devices = winmm->enumerate();
    auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return matchesPort(d, portName); });
    require(found != devices.end(), "MIDIConnect port");
    auto connect = std::make_unique<MIDIConnect>();
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    connect->OpenDevice(found->id);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    connect->SetActive(true);
    require(connect->GetSelectedDevice() == found->id && connect->IsActive(), "MIDIConnect open and active");
    awaitLoopbackReady(midiSend, Source::MidiConnect);
    Collector connected;
    midiSend(0x90, 60, 70);
    midiSend(0x80, 60, 0);
    midiSend(0xB0, 64, 127);
    awaitSamples(connected, Source::MidiConnect, 3);
    require(takeCaptured().size() == 30, "MIDIConnect ten-event messages");
    require(connected.summarize(Source::MidiConnect, frequency()).eventsPerNote == 10, "MIDIConnect event count");
    reportSamples("midiconnect", connected, Source::MidiConnect);
    connect->SetActive(false);
    connect->CloseDevice();
    connect.reset();
    require(dropped() == 0, "production telemetry dropped records");
    stop();
    require(!enabled(), "hook teardown");
    require(start(), "hook restart");
    stop();
    std::cout << "PASS real loopMIDI input, WinRT, WinMM, chords, note-off, sustain, velocity, autoplay and MIDIConnect\n";
}

int wmain(int argc, wchar_t** argv) {
    try {
        injectionPathTests();
        ringTests();
        collectorTests();
        if (argc == 2 && std::wstring(argv[1]) == L"--ui-smoke") { uiTests(); return 0; }
        if (argc == 2 && std::wstring(argv[1]) == L"--list") {
            for (UINT i = 0; i < midiOutGetNumDevs(); ++i) {
                MIDIOUTCAPSW caps{};
                midiOutGetDevCapsW(i, &caps, sizeof(caps));
                std::wcout << L"MIDI output: " << caps.szPname << L'\n';
            }
            for (const auto backend : {MidiBackend::WinRT, MidiBackend::WinMM}) {
                auto input = CreateMidiInput(backend);
                for (const auto& device : input->enumerate())
                    std::wcout << (backend == MidiBackend::WinRT ? L"WinRT input: " : L"WinMM input: ")
                        << device.name << L" | " << device.id << L'\n';
            }
            return 0;
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--legit") { legitModeTests(); return 0; }
        if (argc == 3 && std::wstring(argv[1]) == L"--loopback") { loopbackTests(argv[2]); legitModeTests(); }
        wrapperTests();
        std::cout << "PASS all requested checks\n";
        return 0;
    }
    catch (const std::exception& error) {
        stop();
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
