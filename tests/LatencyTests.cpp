#include "InputLatency.hpp"
#include "MIDI2Key.hpp"
#include "MIDIConnect.hpp"
#include "InputLatencyWindow.hpp"
#include <gdiplus.h>

#include "midi_parser.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
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
// The removed syscall thunk defaulted to a stub that returned 69 and injected
// nothing, so any setup failure silently stopped every keystroke and only the
// traced path noticed. Injection must always route somewhere real.
void injectionPathTests() {
    INPUT none[1]{};
    require(InjectInput != nullptr, "an injection path exists");
    require(InjectInput(0, none, sizeof(INPUT)) == 0, "empty injection reports nothing sent");
    require(InjectInput(0, none, sizeof(INPUT)) != 69, "injection is not the removed no-op stub");
    std::cout << "PASS injection routes through SendInput with no stub to initialize\n";
}

void wrapperTests() {
    TestSink sink;
    require(start(), "measurement hook start");
    const auto original = InjectInput;
    struct Restore { decltype(InjectInput) original; ~Restore() { InjectInput = original; } } restore{original};
    InjectInput = fakeInjection;
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

// The take itself, with no keyboard in it: LegitTake.hpp is a pure function.
void legitTakeTests() {
    using namespace legit;
    // A minute of playing: a bass note and a three-note chord every 400 ms, the
    // chord rolled over 12 ms the way a recording has it, each held 300 ms.
    std::vector<ScoreEvent> score;
    for (int bar = 0; bar < 150; ++bar) {
        const int64_t at = bar * 400'000'000ll;
        const int pitches[4] = {40 + bar % 5, 60, 64, 67};
        for (int n = 0; n < 4; ++n) score.push_back({at + n * 4'000'000, pitches[n], true, 70, 0});
        for (int n = 0; n < 4; ++n) score.push_back({at + 300'000'000 + n * 4'000'000, pitches[n], false, 0, 0});
    }
    std::stable_sort(score.begin(), score.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    const auto displaced = [&](const Take& take, bool pressesOnly = false) {
        int64_t worst = 0;
        for (size_t i = 0; i < score.size(); ++i)
            if (score[i].press || !pressesOnly) worst = std::max<int64_t>(worst, std::llabs(take.events[i].time - score[i].time));
        return worst / 1e6;
    };
    const auto skipped = [&](const Take& take) {
        size_t n = 0;
        for (size_t i = 0; i < score.size(); ++i) if (score[i].press && take.events[i].skip) ++n;
        return n;
    };

    auto off = Defaults(Player::Beginner);
    const auto plain = Build(score, off, 7, 1.0);
    require(displaced(plain) == 0 && skipped(plain) == 0, "with Legit mode off the take is the score");
    for (size_t i = 0; i < score.size(); ++i) require(plain.events[i].mate >= 0, "every press is paired with its release");

    auto pro = Defaults(Player::Pro);
    pro.humanise = true;
    const auto proTake = Build(score, pro, 7, 1.0);
    std::cout << "legit take: Pro moves a press by up to " << displaced(proTake, true) << " ms and a release by up to "
        << displaced(proTake) << " ms\n";
    require(displaced(proTake, true) > 0 && displaced(proTake, true) < 40, "Pro moves every press a little and none far");
    require(displaced(proTake) < 90, "Pro varies a note's length more than its start, and still not far");
    require(skipped(proTake) == 0, "Pro drops nothing");

    auto beginner = Defaults(Player::Beginner);
    beginner.humanise = true;
    beginner.difficulty = 1;
    const auto rough = Build(score, beginner, 7, 1.0);
    require(displaced(rough) < 400, "the loosest take is still bounded: a hesitation is caught up, never accumulated");
    require(skipped(rough) > 0 && skipped(rough) < 30, "Beginner drops a few notes, no two close together");
    for (size_t i = 0; i < score.size(); ++i) {
        const auto& e = rough.events[i];
        // The bass and the top of the chord sound together here, so 60 and 64 are the inner notes.
        if (score[i].press && e.skip) require(score[i].pitch == 60 || score[i].pitch == 64, "only an inner chord note is ever dropped");
        if (!score[i].press) require(e.time >= rough.events[e.mate].time + kMinimumHoldNs, "a key is down before it comes up");
        require(e.velocity >= 0 && e.velocity <= 127, "velocity stays a MIDI velocity");
    }
    // A key is up before it is struck again.
    std::unordered_map<int, int64_t> up;
    std::vector<size_t> order(score.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return rough.events[a].time < rough.events[b].time; });
    std::unordered_map<int, bool> down;
    for (size_t i : order) {
        if (score[i].press) { require(!down[score[i].pitch], "no key is struck while the take still holds it"); down[score[i].pitch] = true; }
        else down[score[i].pitch] = false;
    }

    const auto again = Build(score, beginner, 7, 1.0), other = Build(score, beginner, 8, 1.0);
    bool same = true, differs = false;
    for (size_t i = 0; i < score.size(); ++i) {
        same = same && again.events[i].time == rough.events[i].time && again.events[i].velocity == rough.events[i].velocity;
        differs = differs || other.events[i].time != rough.events[i].time;
    }
    require(same, "one seed is one take");
    require(differs, "another seed is another take");

    // The drift is slow: with only the Tempo slider up, a note's offset is
    // close to its neighbour's, which independent noise would not be.
    Settings drift = Defaults(Player::Student);
    drift.humanise = true; drift.timing = 0; drift.length = 0; drift.mistakes = 0; drift.tempo = 1;
    const auto drifting = Build(score, drift, 7, 1.0);
    double sum = 0, sumSquares = 0, lagged = 0; size_t n = 0; double previous = 0;
    for (size_t i = 0; i < score.size(); ++i) {
        if (!score[i].press) continue;
        const double offset = (drifting.events[i].time - score[i].time) / 1e6;
        if (n) lagged += offset * previous;
        sum += offset; sumSquares += offset * offset; previous = offset; ++n;
    }
    const double mean = sum / n, variance = sumSquares / n - mean * mean;
    require(variance > 1 && (lagged / (n - 1) - mean * mean) / variance > .8, "the tempo drift is slow and correlated, not noise");

    // At twice the speed the same wall-clock offset is twice the score time.
    const auto fast = Build(score, drift, 7, 2.0);
    require(std::abs(displaced(fast) - 2 * displaced(drifting)) < 1, "offsets are wall-clock amounts at any speed");

    // Hands. One track: the bass goes left and the chord right. Two tracks: the
    // file's own division, the higher track the right hand.
    Settings right = off; right.hands = Hands::Right;
    const auto rightOnly = Build(score, right, 7, 1.0);
    for (size_t i = 0; i < score.size(); ++i)
        if (score[i].press) require(rightOnly.events[i].skip == (score[i].pitch < 50), "Right leaves out the bass and nothing else");
    auto twoTracks = score;
    for (auto& e : twoTracks) e.track = e.pitch < 50 ? 1 : 0;
    Settings left = off; left.hands = Hands::Left;
    const auto leftOnly = Build(twoTracks, left, 7, 1.0);
    require(leftOnly.splitByTrack, "two tracks with notes are the two hands");
    for (size_t i = 0; i < twoTracks.size(); ++i)
        if (twoTracks[i].press) require(leftOnly.events[i].skip == (twoTracks[i].track == 0), "Left plays the lower track");

    require(EstimateDifficulty(score, 1.0) > 0 && EstimateDifficulty(score, 2.0) > EstimateDifficulty(score, 1.0),
        "the difficulty estimate rises with the speed");
    std::cout << "PASS legit take: bounded, paired, no stranded or doubled key, seeded, slow drift, speed, hands\n";
}

// Legit mode on the real dispatch path. Needs no MIDI hardware: a synthetic
// score goes through autoplay and the keyboard hook reads back what arrived.
void legitModeTests() {
    legitTakeTests();
    std::cout << "Initializing real PlaybackCore for legit mode..." << std::endl;
    VirtualPianoPlayer player;
    g_player = &player;
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    TestSink sink;
    require(start(), "measurement hook start");
    player.enable_velocity_keypress = false;

    const auto finish = [&] {
        player.should_stop.store(true, std::memory_order_release);
        SetEvent(player.command_event);
        player.playback_thread->join();
        return takeCaptured();
    };
    // Plays a score to completion and returns the injected keyboard events.
    const auto run = [&](std::vector<RawNoteEvent> events) {
        const size_t total = events.size();
        player.note_events = std::move(events);
        player.restart_song();
        const auto deadline = std::chrono::steady_clock::now() + 8s;
        while (player.buffer_index.load(std::memory_order_acquire) < total &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(2ms);
        }
        std::this_thread::sleep_for(100ms);
        return finish();
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
    const auto span = [](const std::vector<KBDLLHOOKSTRUCT>& events) {
        DWORD first = 0, last = 0;
        for (const auto& e : events) {
            if (e.flags & LLKHF_UP) continue;
            if (!first) first = e.time;
            last = e.time;
        }
        return static_cast<long>(last - first);
    };

    const std::vector<RawNoteEvent> chord = {
        {0ns, "C4", EventType::Press, 70, -1}, {0ns, "E4", EventType::Press, 70, -1},
        {0ns, "G4", EventType::Press, 70, -1},
        {80ms, "C4", EventType::Release, 0, -1}, {80ms, "E4", EventType::Release, 0, -1},
        {80ms, "G4", EventType::Release, 0, -1}};
    std::vector<RawNoteEvent> chords;
    for (int i = 0; i < 40; ++i)
        for (const char* note : {"C4", "E4", "G4"}) {
            chords.push_back({std::chrono::milliseconds(i * 60), note, EventType::Press, 70, -1});
            chords.push_back({std::chrono::milliseconds(i * 60 + 40), note, EventType::Release, 0, -1});
        }
    std::stable_sort(chords.begin(), chords.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    std::vector<RawNoteEvent> spaced;
    for (int i = 0; i < 5; ++i) {
        spaced.push_back({std::chrono::milliseconds(i * 200), "C4", EventType::Press, 70, -1});
        spaced.push_back({std::chrono::milliseconds(i * 200 + 100), "C4", EventType::Release, 0, -1});
    }

    // 1. Off is the original path: three presses, three releases, nothing else.
    player.legit_mode_active.store(false, std::memory_order_relaxed);
    auto plain = run(chord);
    require(plain.size() == 6 && downs(plain) == 3 && ups(plain) == 3, "legit off leaves dispatch unchanged");
    const long plainSpan = span(run(spaced));

    // 2. The loosest player at full Mistakes: notes are dropped, and every press
    //    that went out is released. A dropped press takes its release with it
    //    because release_key() only lets go of a key it holds.
    player.legit_seed_override.store(0xA5A5A5A5A5A5A5A5ull, std::memory_order_relaxed);
    auto settings = legit::Defaults(legit::Player::Beginner);
    settings.difficulty = 1;
    settings.mistakes = 1;
    player.set_legit_settings(settings);
    player.legit_mode_active.store(true, std::memory_order_relaxed);
    auto partial = run(chords);
    require(downs(partial) > 80 && downs(partial) < 120, "a few inner notes were dropped");
    require(downs(partial) == ups(partial), "every surviving press was released; no key left held");

    // 3. A take displaces, it does not stretch, and nothing on the dispatch
    //    thread sleeps for it: five notes over 800 ms still span 800 ms.
    const long legitSpan = span(run(spaced));
    std::cout << "legit: dropped=" << (120 - downs(partial)) << "/120 presses, span off/on=" << plainSpan << '/' << legitSpan
        << "ms (score 800ms)\n";
    require(std::abs(plainSpan - 800) < 40 && std::abs(legitSpan - 800) < 120, "the take kept the song's length");

    // 4. Speed is a rate on the clock: at 2 the same score takes half as long,
    //    and the event times were never rewritten.
    player.legit_mode_active.store(false, std::memory_order_relaxed);
    player.note_events = spaced;
    player.restart_song();
    player.requested_speed.store(2.0, std::memory_order_release);
    std::this_thread::sleep_for(700ms);
    const auto fast = finish();
    require(downs(fast) == 5 && std::abs(span(fast) - 400) < 40, "twice the speed is half the time");
    require(player.note_events[8].time == 800ms, "the score was not rescaled");
    player.requested_speed.store(1.0, std::memory_order_release);

    // 5. Tap. The clock stands still; a tap plays the next chord and the key
    //    coming up lets it go. With the recording's lengths the chord lets go
    //    by itself and the key coming up does nothing.
    player.trigger.store(VirtualPianoPlayer::Trigger::Tap, std::memory_order_release);
    player.tap_holds_notes.store(true, std::memory_order_release);
    player.note_events = chords;
    player.restart_song();
    std::this_thread::sleep_for(150ms);
    require(takeCaptured().empty(), "in Tap nothing plays until a tap");
    player.tap(0, true);
    std::this_thread::sleep_for(60ms);
    auto tapped = takeCaptured();
    require(downs(tapped) == 3 && ups(tapped) == 0, "one tap is one chord, held");
    player.tap(1, true);
    std::this_thread::sleep_for(60ms);
    tapped = takeCaptured();
    require(downs(tapped) == 3 && ups(tapped) == 3, "a second key re-strikes the chord; the first still owns its notes");
    player.tap(0, false);
    player.tap(1, false);
    std::this_thread::sleep_for(60ms);
    tapped = takeCaptured();
    require(downs(tapped) == 0 && ups(tapped) == 3, "both keys up lets the chord go");
    player.tap_holds_notes.store(false, std::memory_order_release);
    player.tap(0, true);
    std::this_thread::sleep_for(150ms);
    tapped = takeCaptured();
    require(downs(tapped) == 3 && ups(tapped) == 3, "with the recording's lengths a tapped chord lets go by itself");
    finish();
    player.trigger.store(VirtualPianoPlayer::Trigger::Auto, std::memory_order_release);
    player.tap_holds_notes.store(true, std::memory_order_release);

    player.legit_seed_override.store(0, std::memory_order_relaxed);
    player.set_legit_settings(legit::Defaults(legit::Player::Pro));
    stop();
    g_player = nullptr;
    std::cout << "PASS legit mode: off path, dropped notes paired, length kept, speed on the clock, tap\n";
}

// Does plain playback keep a recording's own timing? Plays the opening of each
// file in a folder through the real dispatch path and compares when each press
// was injected with when the score asked for it. Measures; asserts nothing
// about a file it has never seen beyond every press going out.
void fidelityReport(const std::filesystem::path& folder) {
    VirtualPianoPlayer player;
    g_player = &player;
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    TestSink sink;
    require(start(), "measurement hook start");
    player.enable_velocity_keypress = false;
    player.legit_mode_active.store(false, std::memory_order_relaxed);
    constexpr auto opening = 25s;

    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        auto extension = entry.path().extension().wstring();
        for (auto& c : extension) c = static_cast<wchar_t>(towlower(c));
        if (extension != L".mid" && extension != L".midi") continue;
        MidiParser parser;
        const auto file = parser.parse(entry.path().string());
        player.midi_file = file;
        player.trackMuted.clear();
        player.trackSoloed.clear();
        for (size_t i = 0; i < file.tracks.size(); ++i) {
            player.trackMuted.push_back(std::make_shared<std::atomic<bool>>(false));
            player.trackSoloed.push_back(std::make_shared<std::atomic<bool>>(false));
        }
        player.process_tracks(file);
        std::erase_if(player.note_events, [&](const RawNoteEvent& e) { return e.time > opening; });
        std::vector<double> scoreMs;
        for (const auto& e : player.note_events)
            if (e.action == EventType::Press && e.note_or_control != "sustain")
                scoreMs.push_back(e.time.count() / 1e6);
        std::stable_sort(scoreMs.begin(), scoreMs.end());

        const size_t total = player.note_events.size();
        Collector collector;
        player.restart_song();
        const auto deadline = std::chrono::steady_clock::now() + opening + 5s;
        while (player.buffer_index.load(std::memory_order_acquire) < total &&
               std::chrono::steady_clock::now() < deadline) {
            poll(collector);
            takeCaptured();
            std::this_thread::sleep_for(2ms);
        }
        std::this_thread::sleep_for(200ms);
        player.should_stop.store(true, std::memory_order_release);
        SetEvent(player.command_event);
        player.playback_thread->join();
        player.release_all_keys();
        std::this_thread::sleep_for(50ms);
        poll(collector);
        takeCaptured();

        std::vector<double> sentMs;
        for (const auto& sample : collector.samples(Source::Autoplay))
            if (sample.submission.kind == Kind::NoteOn)
                sentMs.push_back(sample.submission.t1 * 1000.0 / frequency());
        std::sort(sentMs.begin(), sentMs.end());
        std::cout << entry.path().filename().string() << "\n  presses in score " << scoreMs.size()
            << ", injected " << sentMs.size() << '\n';
        if (sentMs.size() != scoreMs.size() || sentMs.size() < 2) continue;

        std::vector<double> error;
        size_t close = 0, merged = 0;
        for (size_t i = 1; i < sentMs.size(); ++i) {
            const double scoreGap = scoreMs[i] - scoreMs[i - 1], sentGap = sentMs[i] - sentMs[i - 1];
            error.push_back(std::abs((sentMs[i] - sentMs[0]) - (scoreMs[i] - scoreMs[0])));
            if (scoreGap > 0 && scoreGap < 3) { ++close; if (sentGap < scoreGap / 2) ++merged; }
        }
        const auto p = percentiles(error);
        std::cout << "  timing error ms p50/p95/p99 " << p.p50 << '/' << p.p95 << '/' << p.p99
            << "  onsets under 3 ms apart " << close << ", sent at under half their gap " << merged << '\n';
    }
    stop();
    g_player = nullptr;
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
        if (argc == 3 && std::wstring(argv[1]) == L"--fidelity") { fidelityReport(argv[2]); return 0; }
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
