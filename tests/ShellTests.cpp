#include "../ui/ShellEngine.hpp"
#include "PlaybackSystem.hpp"
#include "TrackFixture.hpp"
#include "../MIDI++/VelocityTelemetry.hpp"
#include "../MIDI++/WootingAnalog.hpp"
#include "../MIDI++/MidiInput.hpp"
#include "../MIDI++/MidiOutput.hpp"
#include "../MIDI++/SheetExport.hpp"
#include "../MIDI++/MidiStreamSplit.hpp"
#include "../MIDI++/config.hpp"
#include "../MIDI++/MIDI2Key.hpp"
#include "../ui/NativeConnectInput.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <vector>
#include <set>
#include <array>

using namespace std::chrono_literals;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::mutex capturedMutex;
// batch is which injection call the event arrived in. Without it the harness
// cannot tell one call of five events from two calls of four and one, which is
// the only difference the velocity batching makes to what is sent.
struct Captured { INPUT input; DWORD thread; uint64_t batch; };
std::vector<Captured> captured;
uint64_t capturedBatches = 0;
UINT __fastcall Capture(ULONG count, LPINPUT inputs, int) {
    std::lock_guard lock(capturedMutex);
    const uint64_t batch = ++capturedBatches;
    for (ULONG i = 0; i < count; ++i) captured.push_back({inputs[i], GetCurrentThreadId(), batch});
    return count;
}
std::vector<Captured> TakeCaptured() {
    std::lock_guard lock(capturedMutex);
    auto result = std::move(captured);
    captured.clear();
    return result;
}
bool IsNotePress(const Captured& event) {
    const WORD scan = event.input.ki.wScan;
    return !(event.input.ki.dwFlags & KEYEVENTF_KEYUP) && scan &&
        scan != 0x1D && scan != 0x2A && scan != 0x36 && scan != 0x38;
}
template<class F> void Await(F predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(message);
        std::this_thread::sleep_for(5ms);
    }
}

void ModelTests(const std::filesystem::path& fixture) {
    MidiParser parser;
    const auto file = parser.parse(shell::Utf8(fixture));
    auto rows = shell::DescribeTracks(file);
    Require(rows.size() == 5 && rows[0].index == 1 && rows[4].index == 5, "conductor filtering preserves engine indices");
    Require(rows[0].name == "Piano R.H." && rows[0].notes == 1, "track name and velocity-zero note-off");
    Require(rows[0].piano && rows[1].piano && !rows[2].piano, "piano detection");
    Require(rows[2].instrument == "Flute", "program changes from a different track");
    Require(rows[4].drums && !rows[4].piano && rows[4].channels == "10", "channel 10 percussion is not piano program zero");
    shell::SoloPiano(rows);
    Require(shell::SilentTracks(rows) == 3, "Solo Piano mutes non-piano parts");
    rows[2].solo = true;
    Require(shell::SilentTracks(rows) == 4 && shell::TrackAudible(rows[2], true), "solo overrides mute");
    shell::UnmuteAll(rows);
    Require(shell::SilentTracks(rows) == 0 && !shell::AnySolo(rows), "Unmute All clears mute and solo");
    auto mixed = file;
    MidiEvent program;
    program.absoluteTick = 1; program.status = 0xC0; program.data1 = 73;
    MidiEvent note;
    note.absoluteTick = 2; note.status = 0x90; note.data1 = 64; note.data2 = 80;
    mixed.tracks[1].events.push_back(program);
    mixed.tracks[1].events.push_back(note);
    const auto mixedRows = shell::DescribeTracks(mixed);
    Require(!mixedRows[0].piano && mixedRows[0].instrument == "Mixed instruments", "program changes during a part are not mislabeled piano");
    bool rejected = false;
    try { (void)parser.parse(shell::Utf8(fixture.parent_path() / L".." / fixture.filename())); }
    catch (...) { rejected = true; }
    Require(rejected, "path traversal remains rejected");
    rejected = false;
    try { (void)parser.parse(shell::Utf8(fixture) + ":stream"); } catch (...) { rejected = true; }
    Require(rejected, "alternate data streams remain rejected");
    std::cout << "PASS real MIDI parsing, Unicode absolute paths, track indices, programs, drums and solo semantics\n";
}

// release_all_keys used to inject a key-up for every one of the 88 mappings
// whatever was actually down, into whatever window had focus at the time. A
// file load calls it, usually for a score nobody has played a note of, so the
// common case was 88 key-ups that released nothing: measured 9ms a call, and
// that is what clicking a MIDI file spent its time waiting on.
//
// What must stay true is that a key that IS down still comes up. All three
// halves are asserted: nothing held releases nothing, one held note releases
// exactly that one key, and a second call releases nothing again.
void ReleaseAllKeysTests(const std::filesystem::path& config) {
    VirtualPianoPlayer player(false, config);
    player.eightyEightKeyModeActive = true;
    player.enable_velocity_keypress = false;
    const auto noteUps = [](const std::vector<Captured>& events) {
        std::vector<WORD> scans;
        for (const auto& e : events) {
            const WORD scan = e.input.ki.wScan;
            // The unconditional alt and ctrl releases are not note keys.
            if ((e.input.ki.dwFlags & KEYEVENTF_KEYUP) && scan &&
                scan != 0x1D && scan != 0x2A && scan != 0x36 && scan != 0x38) scans.push_back(scan);
        }
        return scans;
    };

    TakeCaptured();
    player.release_all_keys();
    Require(noteUps(TakeCaptured()).empty(), "release_all_keys with nothing held must release nothing");

    // A score of one press and no release, so the note is still held when the
    // playback thread runs out of events and there is something real to free.
    player.trackMuted.push_back(std::make_shared<std::atomic<bool>>(false));
    player.trackSoloed.push_back(std::make_shared<std::atomic<bool>>(false));
    player.note_events = {{0ns, "C4", EventType::Press, 64, 0}};
    TakeCaptured();
    player.restart_song();
    WORD held = 0;
    Await([&] { for (const auto& e : TakeCaptured()) if (IsNotePress(e)) held = e.input.ki.wScan;
                return held != 0; }, "the held note was never pressed");
    player.should_stop = true;
    SetEvent(player.command_event); player.playback_cv.notify_all();
    player.playback_thread->join(); player.playback_thread.reset();
    TakeCaptured();

    player.release_all_keys();
    const auto released = noteUps(TakeCaptured());
    Require(released.size() == 1 && released[0] == held, "release_all_keys releases the held key and only it");

    player.release_all_keys();
    Require(noteUps(TakeCaptured()).empty(), "a key already released is not released twice");

    // The panic path is deliberately the opposite, and the distinction is the
    // point: emergency_exit() is the button you hit when a key is stuck, so it
    // cannot be the one caller that trusts our record of what is stuck. It runs
    // once and exits, so the cost that mattered on the transport path does not
    // matter here.
    player.release_every_mapped_key();
    const auto swept = noteUps(TakeCaptured());
    Require(swept.size() > 40, "the panic path releases every mapped key, held or not");
    // Out-of-range transpose, which the shell is about to make reachable for
    // the first time. press_key folded the note and stored pressed_keys under
    // the folded name; release_key looked up the original. So an A0 pressed the
    // key mapped to A2 and released nothing, and that key stayed down until the
    // panic sweep. Found by the panel seat, 2026-09-07.
    player.ENABLE_OUT_OF_RANGE_TRANSPOSE = true;
    player.eightyEightKeyModeActive = true;
    player.note_events = {{0ns, "A0", EventType::Press, 64, 0}, {20ms, "A0", EventType::Release, 0, 0}};
    TakeCaptured();
    player.restart_song();
    std::vector<Captured> folded;
    const auto isNoteKey = [](const Captured& e) {
        const WORD scan = e.input.ki.wScan;
        // The unconditional alt and ctrl events KeyPress emits are not notes.
        return scan && scan != 0x1D && scan != 0x2A && scan != 0x36 && scan != 0x38;
    };
    Await([&] {
        for (auto& event : TakeCaptured()) folded.push_back(event);
        return std::any_of(folded.begin(), folded.end(), [&](const Captured& e) {
            return isNoteKey(e) && (e.input.ki.dwFlags & KEYEVENTF_KEYUP); });
    }, "the folded note never came back up, which is the stuck key");
    player.should_stop = true;
    SetEvent(player.command_event); player.playback_cv.notify_all();
    player.playback_thread->join(); player.playback_thread.reset();
    // Drain once more: anything sent between the last poll and the join is
    // still sitting in the recorder.
    for (auto& event : TakeCaptured()) folded.push_back(event);

    // A0 is below the 61-key window, so the fold sends it to A2, whose binding
    // is "6". Both halves have to agree on that or the release goes looking for
    // A0's own key and finds nothing pressed.
    constexpr WORD six = 0x07;
    int downs = 0, ups = 0;
    for (const auto& event : folded) {
        const WORD scan = event.input.ki.wScan;
        // The unconditional alt and ctrl events KeyPress emits are not notes.
        if (!scan || scan == 0x1D || scan == 0x2A || scan == 0x36 || scan == 0x38) continue;
        Require(scan == six, "a folded note plays the key its fold chose");
        (event.input.ki.dwFlags & KEYEVENTF_KEYUP) ? ++ups : ++downs;
    }
    Require(downs == 1 && ups == 1, "one press and one release, not a press and silence");

    TakeCaptured();
    player.release_all_keys();
    Require(noteUps(TakeCaptured()).empty(), "nothing is left held for the panic sweep to find");
    player.ENABLE_OUT_OF_RANGE_TRANSPOSE = false;

    std::cout << "PASS release_all_keys releases only the keys that are down, and the panic path releases all of them\n";
}

// MIDI output: the target switch, both call sites, and the id rule.
//
// Everything here runs against a recording IMidiOutput installed through
// SetMidiOutputFactory, which is the same seam and the same reason as
// InjectInput = Capture at the top of wmain: the indirection exists only so
// the thing under test can be driven, and nothing reaches a real device.
struct RecordedMidi {
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> messages;
    bool refuseOpen = false;

    std::vector<std::vector<uint8_t>> take() {
        std::lock_guard lock(mutex);
        std::vector<std::vector<uint8_t>> out;
        out.swap(messages);
        return out;
    }
};

class FakeMidiOutput final : public IMidiOutput {
public:
    explicit FakeMidiOutput(RecordedMidi* sink) : m_sink(sink) {}
    MidiBackend backend() const noexcept override { return MidiBackend::WinRT; }
    std::vector<MidiInputDevice> enumerate() override {
        return { { L"fake:piano", L"Fake Piano", MidiBackend::WinRT, L"Fake Piano" } };
    }
    bool open(const std::wstring& deviceId) override {
        // The absent-device rule the real backends get from ResolveWinMMPort:
        // an id naming something that is not there opens nothing, rather than
        // whatever port happens to be first.
        if (m_sink->refuseOpen || deviceId != L"fake:piano") return false;
        m_openedId = deviceId;
        return true;
    }
    void close() override { m_openedId.clear(); }
    bool isOpen() const noexcept override { return !m_openedId.empty(); }
    const std::wstring& openedDeviceId() const noexcept override { return m_openedId; }
    void send(const uint8_t* message, size_t length) override {
        std::lock_guard lock(m_sink->mutex);
        m_sink->messages.emplace_back(message, message + length);
    }
private:
    RecordedMidi* m_sink;
    std::wstring m_openedId;
};

void MidiOutputTests(const std::filesystem::path& config) {
    using OutputTarget = VirtualPianoPlayer::OutputTarget;

    // The name to number conversion first, because both call sites depend on
    // it and a silent octave error there would present as a device problem.
    //
    // It is the inverse of NOTE_NAME_CACHE and not a second opinion about it,
    // so the bulk of this is a round trip through names built the same way
    // that table builds them, rather than a list of numbers written out again.
    Require(MidiNumberForNoteName("C-1") == 0, "C-1 is note 0");
    Require(MidiNumberForNoteName("G9") == 127, "G9 is note 127");
    Require(MidiNumberForNoteName("C4") == 60, "middle C");
    Require(MidiNumberForNoteName("A#3") == 58, "a sharp is a semitone above the natural");
    Require(MidiNumberForNoteName("H4") == -1, "a letter that is not a note is refused");
    Require(MidiNumberForNoteName("C") == -1, "a name with no octave is refused");
    Require(MidiNumberForNoteName("") == -1 && MidiNumberForNoteName(nullptr) == -1,
            "empty and null are refused rather than answered with note 0");
    Require(MidiNumberForNoteName("G#9") == -1, "a name past 127 is refused, not wrapped");
    {
        static const char* pitches[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        for (int number = 0; number < 128; ++number) {
            char name[8];
            snprintf(name, sizeof(name), "%s%d", pitches[number % 12], (number / 12) - 1);
            Require(MidiNumberForNoteName(name) == number, "a note number did not survive the round trip");
        }
    }

    RecordedMidi sink;
    SetMidiOutputFactory([&](MidiBackend) -> std::unique_ptr<IMidiOutput> {
        return std::make_unique<FakeMidiOutput>(&sink);
    });
    struct FactoryGuard { ~FactoryGuard() { SetMidiOutputFactory({}); } } guard;

    VirtualPianoPlayer player(false, config);
    player.eightyEightKeyModeActive = true;
    // On purpose. The tap must be suppressed on the MIDI target because
    // velocity travels in the note-on byte there, so leaving it enabled is
    // what makes that assertion mean something.
    player.enable_velocity_keypress = true;
    player.trackMuted.push_back(std::make_shared<std::atomic<bool>>(false));
    player.trackSoloed.push_back(std::make_shared<std::atomic<bool>>(false));

    // Collects everything the port has been sent until the predicate is met,
    // because take() drains and a bare call inside a wait would throw away the
    // messages it was waiting for.
    std::vector<std::vector<uint8_t>> seen;
    const auto awaitMessages = [&](size_t count, const char* failure) {
        Await([&] {
            for (auto& message : sink.take()) seen.push_back(std::move(message));
            return seen.size() >= count;
        }, failure);
    };

    Require(!player.open_midi_output(L"fake:absent"),
            "an output id naming an absent device opens nothing");
    Require(player.opened_midi_output().empty(), "and leaves no port behind when it refuses");
    Require(player.open_midi_output(L"fake:piano"), "the present device opens");
    Require(player.opened_midi_output() == L"fake:piano", "and is the one reported open");

    // Opening a port is not switching to it.
    Require(player.output_target.load() == OutputTarget::Keystrokes,
            "opening a port silently redirected output");

    // --- a note on the MIDI target ---------------------------------------
    player.set_output_target(OutputTarget::MidiDevice);
    TakeCaptured();
    sink.take();
    seen.clear();
    // C2 is below the 61-key window, so the keystroke path would fold it up.
    player.note_events = {
        {0ns, "C2", EventType::Press, 100, 0},
        {40ms, "C2", EventType::Release, 0, 0},
    };
    player.restart_song();
    awaitMessages(2, "the note never completed on the MIDI port");
    Require(seen[0].size() == 3 && seen[0][0] == 0x90 && seen[0][1] == 36 && seen[0][2] == 100,
            "velocity reaches the note-on byte, at the pitch that was played");
    Require(seen[1].size() == 3 && seen[1][0] == 0x80 && seen[1][1] == 36,
            "the release is a note-off for the same number");
    for (const auto& event : TakeCaptured())
        Require(!IsNotePress(event), "a note on the MIDI target injected INPUT as well");

    // The fold is a 61-key game constraint and a MIDI device has 128 keys, so
    // it must not reach the wire even with the fold switched on and the
    // limited layout selected, which is the configuration that folds hardest.
    player.eightyEightKeyModeActive = false;
    player.ENABLE_OUT_OF_RANGE_TRANSPOSE = true;
    sink.take();
    seen.clear();
    player.note_events = {
        {0ns, "A#0", EventType::Press, 64, 0},
        {40ms, "A#0", EventType::Release, 0, 0},
    };
    player.restart_song();
    awaitMessages(1, "the out-of-range note never reached the port");
    Require(seen[0][1] == 22, "an out-of-range note went out folded rather than at its own pitch");
    player.eightyEightKeyModeActive = true;

    // --- switching targets is a release ----------------------------------
    //
    // The ordering MIDI-OUTPUT.md warns about. A note held on the outgoing
    // target has to come up on that target, before anything is sent on the
    // new one.
    sink.take();
    seen.clear();
    player.note_events = { {0ns, "E4", EventType::Press, 90, 0} };
    player.restart_song();
    awaitMessages(1, "the held note never started");
    sink.take();
    player.set_output_target(OutputTarget::Keystrokes);
    const auto onSwitch = sink.take();
    Require(!onSwitch.empty(), "switching away from MIDI sent nothing to stop the held note");
    size_t sustainAt = onSwitch.size(), allNotesAt = onSwitch.size();
    for (size_t i = 0; i < onSwitch.size(); ++i) {
        if (onSwitch[i].size() != 3 || (onSwitch[i][0] & 0xF0) != 0xB0) continue;
        if (onSwitch[i][1] == 123 && allNotesAt == onSwitch.size()) allNotesAt = i;
        if (onSwitch[i][1] == 64 && onSwitch[i][2] == 0 && sustainAt == onSwitch.size()) sustainAt = i;
    }
    Require(allNotesAt < onSwitch.size(), "switching away from MIDI does not send All Notes Off");
    Require(sustainAt < onSwitch.size(), "switching away from MIDI does not lift the sustain pedal");
    // A synth holding the pedal keeps ringing through All Notes Off on plenty
    // of hardware, so the pedal comes up first or the room does not go quiet.
    Require(sustainAt < allNotesAt, "sustain must be lifted before All Notes Off, not after");
    Require(player.output_target.load() == OutputTarget::Keystrokes, "the target did not change");

    // --- the keystroke target is unchanged -------------------------------
    sink.take();
    TakeCaptured();
    player.note_events = {
        {0ns, "C4", EventType::Press, 100, 0},
        {40ms, "C4", EventType::Release, 0, 0},
    };
    player.restart_song();
    std::vector<Captured> typed;
    Await([&] {
        for (auto& event : TakeCaptured()) typed.push_back(event);
        return std::any_of(typed.begin(), typed.end(), IsNotePress);
    }, "the keystroke target stopped typing");
    Require(sink.take().empty(), "a note on the keystroke target recorded a MIDI message");

    // Closing the port counts as switching away, so it has to be safe with a
    // note in flight and has to leave the app typing rather than sending into
    // a port that is gone.
    player.set_output_target(OutputTarget::MidiDevice);
    sink.take();
    player.close_midi_output();
    Require(player.output_target.load() == OutputTarget::Keystrokes,
            "closing the port left the app pointed at a device it no longer has");
    Require(player.opened_midi_output().empty(), "and the port is actually gone");
    // Closing goes through the same release as any other switch away, so the
    // last thing the port hears is the stop, sent while it is still open. A
    // close that skipped this would leave a synth sounding with nothing left
    // to tell it otherwise.
    const auto onClose = sink.take();
    Require(std::any_of(onClose.begin(), onClose.end(), [](const std::vector<uint8_t>& message) {
                return message.size() == 3 && (message[0] & 0xF0) == 0xB0 && message[1] == 123;
            }), "closing the port did not stop what it was still playing");

    // And after that, sending is a no-op rather than a crash, which is the
    // state every one of those paths lands in.
    const uint8_t orphan[3] = { 0x90, 60, 100 };
    player.send_midi_output(orphan, 3);
    player.silence_midi_output();
    Require(sink.take().empty(), "output kept flowing after the port was closed");

    std::cout << "PASS MIDI output: note and velocity on the wire, no INPUT, release before switch, absent id refused\n";
}

// The graph has to draw the table the engine will actually use, not a
// resampling of it. The curve the shell plots is the parametric read of the
// threshold table, so the test is that it passes through the table's own
// points: bucket i is reached at input thresholds[i], so the curve at
// x = thresholds[i]/127 must read exactly y = i/31.
//
// Built-ins are read through the player's mapping API, the same path
// ShellEngine.cpp:290 uses, so this cannot pass against a second copy of the
// constants that has drifted from the injector.
void VelocityCurveDrawingTests(const std::filesystem::path& config) {
    VirtualPianoPlayer player(false, config);
    const std::string keys = "1234567890qwertyuiopasdfghjklzxc";
    for (size_t curve = 0; curve < 5; ++curve) {
        shell::VelocityPreset preset{player.getVelocityCurveName(static_cast<midi::VelocityCurveType>(curve))};
        player.setVelocityCurveIndex(curve);
        for (int input = 1; input <= 127; ++input) {
            const size_t output = keys.find(player.getVelocityKey(input));
            for (size_t bucket = output; bucket < 32; ++bucket) preset.thresholds[bucket] = input;
        }
        for (int i = 0; i < 32; ++i) {
            // A repeated threshold names a bucket VelocityBucket can never
            // return, so it is not a point on the curve and nothing is
            // promised about it.
            if (i > 0 && preset.thresholds[i] == preset.thresholds[i - 1]) continue;
            const float y = shell::VelocityCurveAt(preset, preset.thresholds[i] / 127.f);
            Require(std::abs(y - i / 31.f) < 1e-4f,
                    "the drawn curve misses a point the threshold table names");
        }
        // The engine walks the table with <, so input 127 lands in the first
        // bucket holding 127 and the curve has to end there too. Presets that
        // reach 127 early stop below the top of the graph, which is the table
        // being honest rather than the drawing being wrong.
        Require(std::abs(shell::VelocityCurveAt(preset, 1.f) -
                         shell::VelocityBucket(preset.thresholds, 127) / 31.f) < 1e-4f,
                "the curve does not end where the table saturates");
        float previous = -1;
        for (int i = 0; i <= 1270; ++i) {
            const float y = shell::VelocityCurveAt(preset, i / 1270.f);
            Require(y >= previous - 1e-5f, "the drawn curve goes backwards");
            Require(y >= -1e-5f && y <= 1 + 1e-5f, "the drawn curve leaves the graph");
            previous = y;
        }
    }
    // The regression this was reported for. Linear Fine's last interval is 2,
    // 6, 10 ... 122 and then 127, so it is 5 wide where every other is 4. The
    // resampling walked a grid stepping by 4.097 and put its last two samples
    // both at 1.0, drawing that interval flat. Anything that reintroduces a
    // uniform resample fails here.
    player.setVelocityCurveIndex(1);
    shell::VelocityPreset fine{player.getVelocityCurveName(midi::VelocityCurveType::LinearFine)};
    for (int input = 1; input <= 127; ++input) {
        const size_t output = keys.find(player.getVelocityKey(input));
        for (size_t bucket = output; bucket < 32; ++bucket) fine.thresholds[bucket] = input;
    }
    Require(fine.thresholds[30] == 122 && fine.thresholds[31] == 127,
            "Linear Fine is not the table this test was written against");
    Require(shell::VelocityCurveAt(fine, 30 / 31.f) < shell::VelocityCurveAt(fine, 31 / 31.f) - 1e-3f,
            "the top of Linear Fine is flat again");
    std::cout << "PASS velocity curve drawing: every table point hit, saturation, monotonic, Linear Fine tail\n";
}

// A note whose velocity bucket changed used to be two injection calls: the
// four-event ALT tap, then the note. SendInput puts nothing between the events
// of one call and makes no promise at all between two, so anything landing in
// that gap took the velocity the tap had just set. Now it is one call.
//
// Four events is still four events. That is the least a modified keypress can
// be, and shortening it means the game accepting something other than a
// modified keypress, which is not ours to change.
void VelocityBatchTests(const std::filesystem::path& config) {
    VirtualPianoPlayer player(false, config);
    player.enable_velocity_keypress = true;
    player.legit_mode_active = false;
    player.eightyEightKeyModeActive = true;
    player.trackMuted.push_back(std::make_shared<std::atomic<bool>>(false));
    player.trackSoloed.push_back(std::make_shared<std::atomic<bool>>(false));
    // Two notes far enough apart in velocity to land in different buckets, so
    // each one has a tap to send.
    player.note_events = {
        {0ns, "C4", EventType::Press, 20, 0},
        {80ms, "C4", EventType::Release, 0, 0},
        {160ms, "E4", EventType::Press, 120, 0},
        {240ms, "E4", EventType::Release, 0, 0},
    };
    TakeCaptured();
    player.restart_song();

    constexpr WORD ALT_SCAN = 0x38;
    std::vector<Captured> all;
    const auto drain = [&] { for (auto& event : TakeCaptured()) all.push_back(event); };
    // Counting note presses would not do: IsNotePress counts any key down that
    // is not a modifier, and the velocity key is one, so one note would look
    // like two. Count the calls that open with ALT instead, which is one per
    // note whose bucket changed.
    const auto tapCalls = [&] {
        std::set<uint64_t> ids;
        for (const auto& event : all)
            if (event.input.ki.wScan == ALT_SCAN && !(event.input.ki.dwFlags & KEYEVENTF_KEYUP))
                ids.insert(event.batch);
        return ids.size();
    };
    Await([&] { drain(); return tapCalls() >= 2; }, "both notes did not send a velocity tap");
    drain();

    std::map<uint64_t, std::vector<Captured>> batches;
    for (const auto& event : all) batches[event.batch].push_back(event);
    int taps = 0;
    for (const auto& [id, events] : batches) {
        const bool opensWithAlt = !events.empty() && events[0].input.ki.wScan == ALT_SCAN &&
                                  !(events[0].input.ki.dwFlags & KEYEVENTF_KEYUP);
        if (!opensWithAlt) continue;
        ++taps;
        // The regression this exists for: a batch of exactly the four tap
        // events is the tap travelling on its own again.
        Require(events.size() > 4, "the velocity tap must not be an injection call of its own");
        Require(events[1].input.ki.wScan == events[2].input.ki.wScan &&
                !(events[1].input.ki.dwFlags & KEYEVENTF_KEYUP) &&
                (events[2].input.ki.dwFlags & KEYEVENTF_KEYUP),
                "the tap still strikes and releases one velocity key");
        Require(events[3].input.ki.wScan == ALT_SCAN && (events[3].input.ki.dwFlags & KEYEVENTF_KEYUP),
                "the tap still releases ALT, so the note that follows is not typed with it held");
        Require(IsNotePress(events.back()),
                "the note press rides in the same call as the tap that describes it");
        Require(events.back().input.ki.wScan != events[1].input.ki.wScan,
                "the note is a different key from the velocity it was sent with");
    }
    Require(taps >= 2, "both velocity buckets should have sent a tap");
    std::cout << "PASS the velocity tap and its note reach the system as one injection\n";
}

void ReleaseTests(const std::filesystem::path& config) {
    VirtualPianoPlayer player(false, config);
    player.enable_velocity_keypress = false;
    player.legit_mode_active = false;
    player.trackMuted.push_back(std::make_shared<std::atomic<bool>>(false));
    player.trackSoloed.push_back(std::make_shared<std::atomic<bool>>(false));
    const auto noteTest = [&](bool useSolo) {
        player.trackMuted[0]->store(false);
        player.trackSoloed[0]->store(false);
        if (useSolo) {
            player.trackMuted.push_back(std::make_shared<std::atomic<bool>>(false));
            player.trackSoloed.push_back(std::make_shared<std::atomic<bool>>(false));
        }
        player.note_events = {{0ns, "C4", EventType::Press, 80, 0}, {250ms, "C4", EventType::Release, 0, 0}};
        TakeCaptured();
        player.restart_song();
        WORD noteScan = 0;
        Await([&] { for (const auto& e : TakeCaptured()) if (IsNotePress(e)) noteScan = e.input.ki.wScan;
                     return noteScan != 0; }, "note did not press");
        TakeCaptured();
        if (useSolo) player.set_track_solo(1, true);
        else player.set_track_mute(0, true);
        bool released = false;
        Await([&] { for (const auto& e : TakeCaptured())
                        released |= e.input.ki.wScan == noteScan && (e.input.ki.dwFlags & KEYEVENTF_KEYUP);
                     return released; }, "muted or unsoloed track lost its note-off");
        player.should_stop = true;
        SetEvent(player.command_event); player.playback_cv.notify_all();
        player.playback_thread->join(); player.playback_thread.reset();
    };
    noteTest(false);
    noteTest(true);
    // A muted track playing the same pitch must not release an audible track.
    player.trackMuted[0]->store(false);
    player.trackMuted[1]->store(true);
    player.trackSoloed[1]->store(false);
    player.note_events = {{0ns, "C4", EventType::Press, 80, 0},
                          {40ms, "C4", EventType::Press, 80, 1},
                          {80ms, "C4", EventType::Release, 0, 1},
                          {350ms, "C4", EventType::Release, 0, 0}};
    TakeCaptured(); player.restart_song();
    WORD sharedScan = 0;
    Await([&] { for (const auto& e : TakeCaptured()) if (IsNotePress(e)) sharedScan = e.input.ki.wScan;
                 return sharedScan != 0; }, "shared note did not press");
    std::this_thread::sleep_for(170ms);
    for (const auto& e : TakeCaptured())
        Require(e.input.ki.wScan != sharedScan || !(e.input.ki.dwFlags & KEYEVENTF_KEYUP), "muted track released another track's note");
    bool sharedReleased = false;
    Await([&] { for (const auto& e : TakeCaptured())
                    sharedReleased |= e.input.ki.wScan == sharedScan && (e.input.ki.dwFlags & KEYEVENTF_KEYUP);
                 return sharedReleased; }, "audible owner lost its release");
    player.should_stop = true;
    SetEvent(player.command_event); player.playback_cv.notify_all();
    player.playback_thread->join(); player.playback_thread.reset();
    // Inverted sustain uses a Press event to release the key.
    player.trackMuted[0]->store(false);
    player.trackSoloed[1]->store(false);
    player.currentSustainMode = SustainMode::SPACE_UP;
    player.note_events = {{0ns, "sustain", EventType::Release, 0, 0}, {250ms, "sustain", EventType::Press, 127, 0}};
    TakeCaptured();
    player.restart_song();
    bool pedalPressed = false;
    Await([&] { for (const auto& e : TakeCaptured()) pedalPressed |= e.input.ki.wScan == 0x39 && IsNotePress(e);
                 return pedalPressed; }, "inverted pedal did not press");
    TakeCaptured();
    player.set_track_mute(0, true);
    std::this_thread::sleep_for(300ms);
    const auto pedal = TakeCaptured();
    Require(std::any_of(pedal.begin(), pedal.end(), [](const auto& e) { return e.input.ki.dwFlags & KEYEVENTF_KEYUP; }), "inverted sustain releases after mute");
    player.should_stop = true;
    SetEvent(player.command_event); player.playback_cv.notify_all();
    player.playback_thread->join(); player.playback_thread.reset();
    std::cout << "PASS note-off after mute/solo change, shared-pitch ownership and inverted pedal release\n";
}

// Saving a keybind used to reparse and rewrite the whole config, then wait on
// the physical disk, for every single keystroke. That wait is what made
// remapping feel slow, so the write now settles instead. What must not have
// changed with it: the binding applies at once, every binding in a quick run
// still lands, the rest of the config survives, and a crash cannot leave a
// half-written file behind.
void MappingPersistenceTests(const std::filesystem::path& source) {
    const auto config = source.parent_path() / L"config-remap.json";
    std::filesystem::copy_file(source, config, std::filesystem::copy_options::overwrite_existing);
    const auto read = [&] {
        std::ifstream input(config);
        return nlohmann::json::parse(input);
    };
    const auto before = read();
    const auto binding = [](const nlohmann::json& j, const char* note) {
        return j.at("KEY_MAPPINGS").at("FULL").at(note).get<std::string>();
    };

    {
        shell::ShellEngine engine(config);
        // C4, D4 and E4 are 60, 62 and 64. Three in a row, sent as fast as the
        // queue takes them, is the case a debounce could swallow.
        engine.Send({shell::ShellEngine::Action::Remap, {}, 0, 60, false, 0, "z"});
        engine.Send({shell::ShellEngine::Action::Remap, {}, 0, 62, false, 0, "x"});
        engine.Send({shell::ShellEngine::Action::Remap, {}, 0, 64, false, 0, "c"});
        Await([&] { return engine.Snapshot()->mappingRevision >= 3; }, "remaps did not reach the snapshot");
        const auto applied = engine.Snapshot();
        Require(applied->keyMappings.at("C4") == "z" && applied->keyMappings.at("E4") == "c",
                "a binding is applied without waiting for the file");
        Require(applied->error.empty(), "remapping reported an error");

        // The file catches up on its own, without the engine being destroyed.
        Await([&] {
            try { return binding(read(), "E4") == "c"; } catch (const std::exception&) { return false; }
        }, "the settled write never reached the file");
        const auto after = read();
        Require(binding(after, "C4") == "z" && binding(after, "D4") == "x" && binding(after, "E4") == "c",
                "every remap in a quick run is saved, not just the last");
        Require(after.at("VOLUME_SETTINGS") == before.at("VOLUME_SETTINGS") &&
                after.at("HOTKEY_SETTINGS") == before.at("HOTKEY_SETTINGS"),
                "saving a binding preserves the rest of the config");
        Require(!std::filesystem::exists(std::filesystem::path(config).concat(L".shell-tmp")),
                "the temporary file is renamed away, never left beside the config");

        // A remap sent just before shutdown has not settled yet, so the
        // destructor is the only thing that can still write it.
        engine.Send({shell::ShellEngine::Action::Remap, {}, 0, 65, false, 0, "v"});
        Await([&] { return engine.Snapshot()->mappingRevision >= 4; }, "final remap did not reach the snapshot");
    }
    Require(binding(read(), "F4") == "v", "a binding still settling at shutdown is written on the way out");
    std::filesystem::remove(config);
    std::cout << "PASS keybind saves settle, survive shutdown and preserve the config\n";
}

// The folder scan reaches sub-folders. The original app browses instead --
// folders are rows and you click into them -- so this is a deliberate
// difference, not a port of the same behaviour, and it is what the search box
// in that panel is worth having.
//
// The two ways a recursive walk goes wrong are both asserted: a name that does
// not say which folder a file came from, and a walk that does not terminate.
void FolderScanTests(const std::filesystem::path& config) {
    const auto root = std::filesystem::temp_directory_path() / L"midiplusplus-scan-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / L"Classical" / L"Beethoven");
    std::filesystem::create_directories(root / L"empty");
    WriteTrackFixture(root / L"top.mid");
    WriteTrackFixture(root / L"Classical" / L"middle.mid");
    WriteTrackFixture(root / L"Classical" / L"Beethoven" / L"deep.midi");
    // Neither of these is a score, and both are the kind of thing that sits in
    // a music folder.
    std::ofstream(root / L"notes.txt") << "not a midi file";
    std::ofstream(root / L"Classical" / L"cover.jpg") << "not a midi file either";

    shell::ShellEngine engine(config);
    engine.Send({shell::ShellEngine::Action::Scan, root});
    Await([&] { const auto s = engine.Snapshot(); return !s->busy && s->files->size() >= 3; },
          "recursive folder scan timeout");
    const auto found = engine.Snapshot()->files;
    Require(found->size() == 3, "every .mid and .midi below the folder is listed, and nothing else is");

    std::set<std::string> names;
    for (const auto& entry : *found) {
        names.insert(entry.name);
        Require(std::filesystem::exists(entry.path), "a listed file's path resolves");
        Require(entry.bytes > 0, "a listed file reports its size");
    }
    // The name carries the sub-folder, or two files called the same thing in
    // two folders would be one indistinguishable row twice. Windows separators,
    // because that is what the panel shows the user.
    Require(names.count("top.mid") == 1, "a file in the chosen folder keeps its plain name");
    Require(names.count("Classical\\middle.mid") == 1, "a file one level down is named by its relative path");
    Require(names.count("Classical\\Beethoven\\deep.midi") == 1, "and so is one two levels down");

    // A directory symlink pointing at its own parent is the shape that makes a
    // naive recursive walk run until it runs out of path. Skipped on machines
    // without the privilege to create one, which is most of them unmodified.
    std::error_code link;
    std::filesystem::create_directory_symlink(root, root / L"Classical" / L"loop", link);
    if (!link) {
        shell::ShellEngine second(config);
        second.Send({shell::ShellEngine::Action::Scan, root});
        Await([&] { const auto s = second.Snapshot(); return !s->busy && !s->files->empty(); },
              "scan did not terminate with a directory symlink loop present");
        Require(second.Snapshot()->files->size() < 100, "a symlink loop does not multiply the listing");
    }

    std::filesystem::remove_all(root);
    std::cout << "PASS folder scan reaches sub-folders, names them by relative path and terminates on a loop\n";
}

void ControllerTests(const std::filesystem::path& config, const std::filesystem::path& fixture) {
    shell::ShellEngine engine(config);
    const DWORD uiThread = GetCurrentThreadId();
    engine.Send({shell::ShellEngine::Action::Load, fixture, 0, 0, true});
    Await([&] { const auto s = engine.Snapshot(); return !s->busy && (!s->loaded.empty() || !s->error.empty()); }, "async load timeout");
    auto state = engine.Snapshot();
    if (!state->error.empty()) throw std::runtime_error(state->error);
    Require(shell::SilentTracks(state->rows) == 3, "auto Solo Piano on load");
    const auto generation = state->generation;

    engine.Send({shell::ShellEngine::Action::Velocity, {}, 0, 0, false});
    Await([&] { return !engine.Snapshot()->velocity; }, "velocity command not consumed");
    TakeCaptured();
    engine.Send({shell::ShellEngine::Action::Play, {}, generation});
    Await([&] { return engine.Snapshot()->playing; }, "play command not consumed");
    Await([&] { return !engine.Snapshot()->playing; }, "playback did not finish");
    const auto events = TakeCaptured();
    Require(std::count_if(events.begin(), events.end(), IsNotePress) == 2, "Solo Piano dispatch must contain exactly two piano notes");
    for (const auto& event : events) Require(event.thread != uiThread, "injection on message thread");
    engine.Send({shell::ShellEngine::Action::UnmuteAll, {}, generation});
    Await([&] { return shell::SilentTracks(engine.Snapshot()->rows) == 0; }, "Unmute All command");
    engine.Send({shell::ShellEngine::Action::Play, {}, generation});
    Await([&] { return engine.Snapshot()->playing; }, "second play command not consumed");
    Await([&] { return !engine.Snapshot()->playing; }, "second playback did not finish");
    const auto allNotes = TakeCaptured();
    Require(std::count_if(allNotes.begin(), allNotes.end(), IsNotePress) == 5, "Unmute All dispatch must include all five parts");
    engine.Send({shell::ShellEngine::Action::Solo, {}, generation, 3, true});
    Await([&] { return shell::SilentTracks(engine.Snapshot()->rows) == 4; }, "solo routed by original track index");
    engine.Send({shell::ShellEngine::Action::Load, fixture, 0, 0, false});
    Await([&] { return engine.Snapshot()->generation > generation && !engine.Snapshot()->busy; }, "reload timeout");
    engine.Send({shell::ShellEngine::Action::SoloPiano, {}, generation});
    engine.Send({shell::ShellEngine::Action::Scan, fixture.parent_path()});
    Await([&] { return !engine.Snapshot()->files->empty(); }, "folder scan timeout");
    Require(shell::SilentTracks(engine.Snapshot()->rows) == 0, "stale row commands cannot affect a new file");
    engine.Send({shell::ShellEngine::Action::Load, fixture.parent_path() / L"missing.mid"});
    Await([&] { return !engine.Snapshot()->error.empty(); }, "load error not reported");
    Require(engine.Snapshot()->loaded == fixture && engine.Snapshot()->rows.size() == 5, "failed parse preserves previous score");
    std::cout << "PASS async loading, engine dispatch off UI thread, track commands, generation checks and error recovery\n";
}

// The Wooting keyboard has no notes of its own: a key sounds whatever the
// user's mapping would type for it. The layout's number row is the bottom of
// the range, and having it at the top is what made "1" play A5 and the run
// 1 2 3 jump an octave between 2 and 3.
void WootingMapTests() {
    const auto map = DefaultWootingScancodeNoteMap();
    const auto note = [&](uint16_t scancode) { return static_cast<int>(map[scancode]); };
    Require(note(0x02) == 36, "1 is C2, the bottom of the range");
    Require(note(0x03) == 38 && note(0x04) == 40 && note(0x05) == 41, "2 3 4 continue the C major scale");
    Require(note(0x06) == 43 && note(0x07) == 45, "5 and 6 stay inside the same octave as 1");
    Require(note(0x0B) == 52, "0 is E3, not the top of the keyboard");
    Require(note(0x10) == 53, "q follows 0 rather than restarting");
    Require(note(0x32) == 96, "m is the top of the unshifted layout");
    // Black keys are shifted bindings, and a shifted key is two physical keys
    // to the analog SDK, so they stay unmapped rather than guessed. Shift
    // amount is what reaches them: it is a note offset, not a note.
    Require(note(0x2A) == -1 && note(0x1D) == -1, "shift and ctrl are not notes");
    Require(map[kWootingShiftScancode] == -1, "the shift key never sounds a note of its own");

    // A user's own mapping wins over the built-in layout.
    std::map<std::string, std::string> mapping{
        {"C2", "1"}, {"D2", "2"}, {"C#2", "!"}, {"A0", "ctrl+1"}, {"C8", "m"}};
    const auto custom = WootingScancodeNoteMapFrom(mapping);
    Require(custom[0x02] == 36 && custom[0x03] == 38, "unshifted bindings carry their note");
    Require(custom[0x32] == 108, "C8 reaches the top of the range");
    Require(custom[0x10] == -1, "keys the mapping does not name stay silent");
    for (const auto& entry : custom) Require(entry >= -1 && entry <= 127, "no note escapes the MIDI range");

    // The two layouts bind the same characters to different notes, so which
    // one the map is built from decides what a Wooting key sounds. MIDI2Key
    // built it from full_key_mappings unconditionally, which was invisible
    // only because the shell pinned 88-key mode on. These are the real
    // config.json bindings for the character "t".
    const std::map<std::string, std::string> full{{"C4", "t"}, {"G3", "w"}};
    const std::map<std::string, std::string> limited{{"C4", "t"}, {"G3", "w"}, {"C7", "m"}};
    Require(WootingScancodeNoteMapFrom(full)[0x14] == 60, "t is C4 in the 88-key layout");
    Require(WootingScancodeNoteMapFrom(limited)[0x32] == 96,
            "the 61-key layout binds m to C7, which the 88-key layout does not");
    Require(WootingScancodeNoteMapFrom(full)[0x32] != WootingScancodeNoteMapFrom(limited)[0x32],
            "the layouts disagree, so picking the wrong one sounds the wrong note");

    std::cout << "PASS wooting scancode mapping follows the virtual piano layout\n";
}

// The Wooting poll loop, which until now had no test at all: the loop needs a
// keyboard on the desk, so everything it decides was written and shipped
// unexercised. WootingPollStep is that loop without the SDK or the clock.
void WootingPollTests() {
    // A tiny map: "1" is C2, "2" is D2, and nothing else is a note.
    constexpr uint16_t SC_1 = 0x02, SC_2 = 0x03, SC_ESC = 0x01;
    std::array<int16_t, 256> map{};
    map.fill(-1);
    map[SC_1] = 36;   // C2
    map[SC_2] = 38;   // D2

    WootingAnalogSettings settings{};   // trigger 0.5, release fraction 0.6, shift 12, scale 5
    std::array<WootingPollEvent, 300> out{};
    const auto poll = [&](WootingPollState& state, std::vector<uint16_t> codes,
                          std::vector<float> values, double seconds = 0.001) {
        std::vector<WootingPollEvent> events;
        const size_t count = WootingPollStep(state, codes.data(), values.data(),
                                             static_cast<int>(codes.size()), map, settings,
                                             seconds, out.data(), out.size());
        for (size_t i = 0; i < count; ++i) events.push_back(out[i]);
        return events;
    };

    // A key below the trigger is not a note yet, and crossing it is.
    {
        WootingPollState state;
        Require(poll(state, {SC_1}, {0.40f}).empty(), "a key short of the trigger sounds nothing");
        auto struck = poll(state, {SC_1}, {0.80f});
        Require(struck.size() == 1 && struck[0].on && struck[0].note == 36, "crossing the trigger sounds the mapped note");
        Require(struck[0].velocity > 0, "a struck note carries a velocity");
        Require(poll(state, {SC_1}, {0.95f}).empty(), "a key already down does not sound again");
    }

    // The release gap is what stops a key resting on the trigger from
    // stuttering. Default is 0.6 of it, so 0.30.
    {
        WootingPollState state;
        poll(state, {SC_1}, {0.80f});
        Require(poll(state, {SC_1}, {0.45f}).empty(), "falling below the trigger is not yet a release");
        Require(poll(state, {SC_1}, {0.31f}).empty(), "nor is anything above the release");
        auto released = poll(state, {SC_1}, {0.30f});
        Require(released.size() == 1 && !released[0].on && released[0].note == 36, "reaching the release lets the note go");
        auto again = poll(state, {SC_1}, {0.80f});
        Require(again.size() == 1 && again[0].on, "and the key can then be struck again");
    }

    // A key that vanishes from the buffer has been let go. The SDK reports only
    // keys that are off the rest, so this is the ordinary way a note ends.
    {
        WootingPollState state;
        poll(state, {SC_1}, {0.80f});
        auto gone = poll(state, {}, {});
        Require(gone.size() == 1 && !gone[0].on && gone[0].note == 36, "a key leaving the buffer releases its note");
        Require(poll(state, {}, {}).empty(), "and does not release it twice");
    }

    // Shift amount, which is the whole reason a Wooting can play a black key:
    // the layout only reaches the naturals.
    {
        settings.shiftAmount = 1;
        WootingPollState state;
        auto sharp = poll(state, {kWootingShiftScancode, SC_1}, {0.90f, 0.80f});
        Require(sharp.size() == 1 && sharp[0].on && sharp[0].note == 37, "holding shift plays the note above");
        // Letting shift go while the key is still down must release the note
        // that was actually sounded. Releasing 36 would leave 37 held in the
        // game with nothing left to release it.
        auto let = poll(state, {SC_1}, {0.10f});
        Require(let.size() == 1 && !let[0].on && let[0].note == 37,
                "the note off matches the note on, not what the map says now");
        settings.shiftAmount = 12;
    }

    // A shift arriving after the key is down does not retune a sounding note,
    // which is what the upstream app does and for the same reason.
    {
        WootingPollState state;
        auto plain = poll(state, {SC_1}, {0.80f});
        Require(plain.size() == 1 && plain[0].note == 36, "struck without shift");
        Require(poll(state, {kWootingShiftScancode, SC_1}, {0.90f, 0.85f}).empty(),
                "shifting mid-note changes nothing while the key is held");
        auto let = poll(state, {kWootingShiftScancode, SC_1}, {0.90f, 0.05f});
        Require(let.size() == 1 && !let[0].on && let[0].note == 36, "and it still releases the note it sounded");
    }

    // A shift below the trigger is not held. The shift is an analog key too.
    {
        settings.shiftAmount = 1;
        WootingPollState state;
        auto unshifted = poll(state, {kWootingShiftScancode, SC_1}, {0.20f, 0.80f});
        Require(unshifted.size() == 1 && unshifted[0].note == 36, "a shift key barely touched is not held");
        settings.shiftAmount = 12;
    }

    // A shift that pushes a key off the MIDI range plays nothing, and leaves
    // nothing behind to release.
    {
        settings.shiftAmount = 127;
        WootingPollState state;
        Require(poll(state, {kWootingShiftScancode, SC_1}, {0.90f, 0.80f}).empty(),
                "a note shifted past 127 is silent rather than wrapped");
        Require(poll(state, {}, {}).empty(), "and a silent key releases nothing when let go");
        settings.shiftAmount = -127;
        WootingPollState below;
        Require(poll(below, {kWootingShiftScancode, SC_1}, {0.90f, 0.80f}).empty(),
                "and the same below zero");
        settings.shiftAmount = 12;
    }

    // The shift key itself is not a note, and neither is anything unmapped.
    {
        WootingPollState state;
        Require(poll(state, {kWootingShiftScancode}, {0.90f}).empty(), "the shift key sounds nothing of its own");
        Require(poll(state, {SC_ESC}, {0.90f}).empty(), "a key the map does not name sounds nothing");
    }

    // Two keys at once are two notes, and each is released on its own.
    {
        WootingPollState state;
        auto both = poll(state, {SC_1, SC_2}, {0.80f, 0.90f});
        Require(both.size() == 2 && both[0].on && both[1].on, "two keys struck together are two note ons");
        Require((both[0].note == 36 && both[1].note == 38), "each key sounds its own note");
        auto one = poll(state, {SC_1, SC_2}, {0.80f, 0.05f});
        Require(one.size() == 1 && !one[0].on && one[0].note == 38, "letting one go leaves the other sounding");
    }

    // Velocity comes from how fast the key was travelling, so the same depth
    // reached faster is louder. Depth alone cannot work: every key crosses the
    // trigger at the same depth.
    {
        WootingPollState fast, slow;
        auto quick = poll(fast, {SC_1}, {0.80f}, 0.002);
        auto gentle = poll(slow, {SC_1}, {0.80f}, 0.100);
        Require(quick[0].velocity > gentle[0].velocity, "a faster strike is a louder note");
    }

    std::cout << "PASS wooting poll: trigger, release gap, shift, dropped keys and strike velocity\n";
}

// Sheet text is what people paste to each other for these games, and it is the
// half of the YouTube to MIDI pipeline that needs nothing installed. A note is
// the character the app would type for it, notes struck together are bracketed,
// and time is spaces.
void SheetExportTests() {
    const std::map<std::string, std::string> mapping{
        {"C4", "t"}, {"E4", "y"}, {"G4", "u"}, {"C5", "i"}, {"C#4", "%"}};

    // A chord is one bracket, and its characters are ordered so the same chord
    // always reads the same way whatever order the notes arrived in.
    auto chord = sheet::ToVirtualPiano({{0.010, "G4"}, {0.0, "C4"}, {0.020, "E4"}}, mapping);
    Require(chord.text == "[tuy]", "notes struck together are one sorted bracket");
    Require(chord.groups == 1 && chord.notes == 3, "a chord is one group of three notes");

    // Far enough apart and they are separate groups, and a single note carries
    // no brackets.
    auto melody = sheet::ToVirtualPiano({{0.0, "C4"}, {0.5, "E4"}, {1.0, "G4"}}, mapping);
    Require(melody.text == "t y u", "separate onsets are separate groups");
    Require(melody.groups == 3, "three notes, three groups");

    // The window is the boundary, not a suggestion. 45 ms is the default and
    // comes from measured chord asynchrony.
    sheet::Options tight; tight.chordWindow = 0.045;
    auto edge = sheet::ToVirtualPiano({{0.0, "C4"}, {0.045, "E4"}}, mapping, tight);
    Require(edge.groups == 1, "exactly the window is still one chord");
    auto past = sheet::ToVirtualPiano({{0.0, "C4"}, {0.046, "E4"}}, mapping, tight);
    Require(past.groups == 2, "past the window is two groups");

    // With a tempo, a gap of whole beats becomes extra spaces. Without one,
    // every gap is a single space, because guessing a tempo would be inventing
    // rhythm that is not in the input.
    sheet::Options timed; timed.beatSeconds = 0.5;
    auto spaced = sheet::ToVirtualPiano({{0.0, "C4"}, {0.5, "E4"}, {2.0, "G4"}}, mapping, timed);
    Require(spaced.text == "t y   u", "a gap of three beats widens to three spaces");
    auto untimed = sheet::ToVirtualPiano({{0.0, "C4"}, {0.5, "E4"}, {2.0, "G4"}}, mapping);
    Require(untimed.text == "t y u", "with no tempo every gap is one space");

    // A long silence must not produce a line of nothing but spaces.
    sheet::Options capped; capped.beatSeconds = 0.5; capped.maxGapSpaces = 2;
    auto silence = sheet::ToVirtualPiano({{0.0, "C4"}, {60.0, "E4"}}, mapping, capped);
    Require(silence.text == "t  y", "a long silence is capped rather than unbounded");

    // Notes the mapping does not carry are dropped and counted, never guessed
    // at and never silently lost.
    auto missing = sheet::ToVirtualPiano({{0.0, "C4"}, {0.5, "A9"}, {1.0, "E4"}}, mapping);
    Require(missing.text == "t y", "an unmapped note leaves no mark in the sheet");
    Require(missing.unmapped == 1 && missing.notes == 2, "and is reported rather than lost");

    // Black keys are shifted characters and go through as they are, which is
    // what a sheet reader types.
    auto sharp = sheet::ToVirtualPiano({{0.0, "C#4"}}, mapping);
    Require(sharp.text == "%", "a shifted binding is written as its own character");

    // Lines are short enough to read. A break already separates two groups, so
    // it must not also carry a space.
    sheet::Options lines; lines.groupsPerLine = 2;
    auto wrapped = sheet::ToVirtualPiano({{0.0, "C4"}, {0.5, "E4"}, {1.0, "G4"}, {1.5, "C5"}}, mapping, lines);
    Require(wrapped.text == "t y\nu i", "lines wrap without a trailing space");

    // Unsorted input is sorted, and an empty score is an empty sheet rather
    // than a stray separator.
    auto unsorted = sheet::ToVirtualPiano({{1.0, "G4"}, {0.0, "C4"}, {0.5, "E4"}}, mapping);
    Require(unsorted.text == "t y u", "onsets are ordered before anything is written");
    auto empty = sheet::ToVirtualPiano({}, mapping);
    Require(empty.text.empty() && empty.groups == 0, "no notes is no sheet");
    auto allMissing = sheet::ToVirtualPiano({{0.0, "A9"}}, mapping);
    Require(allMissing.text.empty() && allMissing.unmapped == 1, "a score of nothing mappable writes nothing");

    // Two notes in one chord that map to the same character are typed once,
    // because "[tt]" is not a chord anyone can play. They are neither written
    // nor unmapped, so counting only the two buckets lost them: on the owner's
    // own files that was 5370 of 5371 note-ons and 1581 of 1586, with zero
    // reported unmapped, which read as the exporter silently dropping notes.
    const std::map<std::string, std::string> shared{
        {"C4", "t"}, {"B3", "t"}, {"E4", "y"}};
    auto doubled = sheet::ToVirtualPiano({{0.0, "C4"}, {0.010, "B3"}, {0.020, "E4"}}, shared);
    Require(doubled.text == "[ty]", "a repeated character in a chord is typed once");
    Require(doubled.notes == 2 && doubled.merged == 1 && doubled.unmapped == 0,
            "the note that shared a character is counted as merged, not lost");

    // Outside the chord window the same character is written twice, because
    // then it really is struck twice.
    auto apart = sheet::ToVirtualPiano({{0.0, "C4"}, {1.0, "B3"}}, shared);
    Require(apart.text == "t t" && apart.notes == 2 && apart.merged == 0,
            "the same key struck twice is two notes, not a merge");

    // The invariant the three counters exist for: every note handed in lands in
    // exactly one of written, merged or unmapped.
    const std::vector<sheet::Note> mixed{
        {0.0, "C4"}, {0.005, "B3"}, {0.010, "E4"},   // chord, one merge
        {0.5, "A9"},                                  // unmapped
        {1.0, "C4"}, {1.005, "B3"},                   // chord, one merge
        {2.0, "E4"}};
    auto total = sheet::ToVirtualPiano(mixed, shared);
    Require(total.notes + total.merged + total.unmapped == mixed.size(),
            "notes + merged + unmapped accounts for every note handed in");
    Require(total.merged == 2 && total.unmapped == 1, "and each lands in the right one");

    std::cout << "PASS MIDI to virtual piano sheet: chords, spacing, wrapping, merged and unmapped notes\n";
}

// Three index spaces once disagreed about what "device 1" meant, which is
// harmless with one device and wrong with two. Ids fixed the disagreement but
// not the resolution: RtMidi welds the port index onto every WinMM port name,
// so the name half of an id carried the very number it existed to outlive, and
// a device that had gone away resolved to whichever port was left.
// A Kernel Streaming pin hands over bytes, not messages, because that is what
// the wire carries. WinMM and WinRT both hand over messages, and everything
// above IMidiInput is written for messages, so the KS backend cuts the stream
// up. These are the three cases a real keyboard will not produce on demand.
void MidiStreamSplitTests() {
    std::vector<std::vector<uint8_t>> out;
    midi_stream::Splitter splitter;
    const auto feed = [&](std::vector<uint8_t> bytes) {
        out.clear();
        splitter.feed(bytes.data(), bytes.size(),
                      [&](const uint8_t* m, size_t n) { out.emplace_back(m, m + n); });
        return out;
    };

    auto plain = feed({0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00});
    Require(plain.size() == 2, "two whole messages come out as two");
    Require(plain[0] == std::vector<uint8_t>({0x90, 0x3C, 0x64}), "note on is delivered entire");
    Require(plain[1] == std::vector<uint8_t>({0x80, 0x3C, 0x00}), "and so is note off");

    // Running status: the status byte is sent once and the data pairs that
    // follow are all the same kind of message. A keyboard playing a chord does
    // this constantly, and reading it wrong turns notes into silence.
    splitter.reset();
    auto running = feed({0x90, 0x3C, 0x64, 0x40, 0x50, 0x43, 0x55});
    Require(running.size() == 3, "running status yields one message per data pair");
    Require(running[1] == std::vector<uint8_t>({0x90, 0x40, 0x50}) &&
            running[2] == std::vector<uint8_t>({0x90, 0x43, 0x55}), "and repeats the held status");

    // A realtime byte may arrive between any two bytes of another message and
    // must disturb neither it nor running status.
    splitter.reset();
    auto interrupted = feed({0x90, 0x3C, 0xF8, 0x64});
    Require(interrupted.size() == 2, "the clock byte and the note it split are both delivered");
    Require(interrupted[0] == std::vector<uint8_t>({0xF8}), "the clock comes out on its own");
    Require(interrupted[1] == std::vector<uint8_t>({0x90, 0x3C, 0x64}), "and the note is still whole");

    // A message split across two reads is one message, not two halves.
    splitter.reset();
    Require(feed({0x90, 0x3C}).empty(), "half a message emits nothing yet");
    auto completed = feed({0x64});
    Require(completed.size() == 1 && completed[0] == std::vector<uint8_t>({0x90, 0x3C, 0x64}),
            "and completes on the byte that finishes it, across reads");

    // System exclusive is dropped rather than delivered in fragments the
    // callers have nowhere to put, and must not swallow what follows it.
    splitter.reset();
    auto sysex = feed({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7, 0x90, 0x3C, 0x64});
    Require(sysex.size() == 1 && sysex[0] == std::vector<uint8_t>({0x90, 0x3C, 0x64}),
            "sysex is skipped and the next real message still arrives");

    // System Common cancels running status: a data byte after one is not the
    // start of another note.
    splitter.reset();
    feed({0x90, 0x3C, 0x64});
    auto afterCommon = feed({0xF6, 0x40, 0x50});
    Require(afterCommon.size() == 1 && afterCommon[0] == std::vector<uint8_t>({0xF6}),
            "tune request cancels running status rather than borrowing it");

    // Two-byte and one-byte channel messages are counted by their own status.
    splitter.reset();
    auto program = feed({0xC0, 0x07, 0xD0, 0x40});
    Require(program.size() == 2 && program[0].size() == 2 && program[1].size() == 2,
            "program change and channel pressure carry one data byte each");

    std::cout << "PASS MIDI byte stream split into messages: running status, realtime, sysex and partial reads\n";
}

// The KSMUSICFORMAT walk, and the reason the Kernel Streaming backend was
// "incredibly broken" on a real piano on 2026-09-06.
//
// The read buffer outlives the read and a read overwrites only the bytes it
// produced, so past DataUsed the buffer still holds the previous read's
// events. The walk was bounded by DataUsed alone. A driver reporting it high,
// by any amount, replays whatever is sitting there: the tester played a single
// note and the game showed about eighteen, which is how many old events fit.
//
// No driver can be asked to report a wrong length on demand. Driven here it is
// one array and one number.
void KsEventWalkTests() {
    std::vector<std::vector<uint8_t>> out;
    midi_stream::Splitter splitter;
    const auto emit = [&](const uint8_t* m, size_t n) { out.emplace_back(m, m + n); };

    // Three note-ons as the driver frames them: an 8-byte header each, payload
    // padded up to 4. Three bytes pads to four, so each event is 12.
    std::array<uint8_t, 64> buffer{};
    const auto writeEvent = [&](size_t at, uint8_t note) {
        const midi_stream::KsMusicHeader header{0, 3};
        std::memcpy(buffer.data() + at, &header, sizeof(header));
        buffer[at + 8] = 0x90; buffer[at + 9] = note; buffer[at + 10] = 100;
        return at + 12;
    };
    size_t used = writeEvent(0, 60);
    used = writeEvent(used, 64);
    used = writeEvent(used, 67);
    Require(used == 36, "three framed events are 36 bytes");

    out.clear();
    midi_stream::FeedKsEvents(splitter, buffer.data(), used, emit);
    Require(out.size() == 3 && out[0][1] == 60 && out[2][1] == 67, "a full read yields its three notes");

    // The read that broke it: one new note written over the front of a buffer
    // that still holds the three above, with the driver claiming the whole
    // buffer. Clearing first is what makes the rest read as a zero count.
    std::memset(buffer.data(), 0, buffer.size());
    const size_t one = writeEvent(0, 72);
    out.clear();
    midi_stream::FeedKsEvents(splitter, buffer.data(), buffer.size(), emit);
    Require(out.size() == 1 && out[0][1] == 72,
            "one note played is one note delivered, whatever length the driver claims");

    // And unclear, the same read is every note still in the buffer. This is
    // the tester's screenshot, and it is what the memset in readLoop prevents.
    writeEvent(one, 64);
    writeEvent(one + 12, 67);
    out.clear();
    midi_stream::FeedKsEvents(splitter, buffer.data(), buffer.size(), emit);
    Require(out.size() == 3, "an uncleared buffer is exactly the reported bug, so the clear is load bearing");

    // A length that runs off the end ends the walk. Clamping it would hand the
    // splitter bytes the driver never wrote.
    std::memset(buffer.data(), 0, buffer.size());
    const midi_stream::KsMusicHeader overrun{0, 1000};
    std::memcpy(buffer.data(), &overrun, sizeof(overrun));
    out.clear();
    midi_stream::FeedKsEvents(splitter, buffer.data(), buffer.size(), emit);
    Require(out.empty(), "an event longer than the read is refused, not clamped");

    // A trailing fragment is held for the next read rather than dropped, which
    // is the behaviour running status depends on.
    std::memset(buffer.data(), 0, buffer.size());
    const midi_stream::KsMusicHeader partial{0, 2};
    std::memcpy(buffer.data(), &partial, sizeof(partial));
    buffer[8] = 0x90; buffer[9] = 55;
    out.clear();
    splitter.reset();
    midi_stream::FeedKsEvents(splitter, buffer.data(), 12, emit);
    Require(out.empty() && splitter.assembling(), "half a message waits for the rest of it");

    std::cout << "PASS Kernel Streaming event walk: framing, a stale buffer, overruns and split messages\n";
}

// Every id a backend produces must route back to that backend, or the app opens
// the wrong device -- the bug this whole interface exists to prevent. Machines
// without a KS MIDI pin enumerate nothing, and an empty list is a pass: there
// is nothing to assert about a transport that is not there.
void KernelStreamingIdentityTests() {
    auto kernel = CreateKernelStreamingInput();
    Require(kernel != nullptr, "the Kernel Streaming backend can be constructed");
    Require(kernel->backend() == MidiBackend::KernelStreaming, "and reports itself");
    Require(!kernel->isOpen() && kernel->openedDeviceId().empty(), "a fresh backend holds nothing open");

    size_t pins = 0;
    for (const auto& device : kernel->enumerate()) {
        ++pins;
        Require(device.backend == MidiBackend::KernelStreaming, "an enumerated pin says which backend it came from");
        Require(BackendForDeviceId(device.id) == MidiBackend::KernelStreaming,
                "and its id routes back to that backend rather than the WinRT default");
        Require(!device.name.empty(), "a pin offered to the user has a name");
    }

    // An id naming a device that is not there fails, rather than opening a
    // different pin. Same rule the WinMM and WinRT backends are held to.
    Require(!kernel->open(L"ks:0|\\?\nothing#is#here", [](uint64_t, const uint8_t*, size_t) {}),
            "an id that names nothing opens nothing");
    Require(!kernel->open(L"winmm:0|Piano", [](uint64_t, const uint8_t*, size_t) {}),
            "and another backend's id is refused rather than guessed at");
    Require(!kernel->isOpen(), "a refused open leaves nothing open");

    std::cout << "PASS Kernel Streaming enumeration, id routing and refusal (" << pins << " MIDI pins present)\n";
}

void PortResolutionTests() {
    const std::vector<std::wstring> present{L"MIDI 0", L"loopMIDI Port 1"};

    // RtMidi's suffix comes off; a number that belongs to the device stays on.
    Require(StripRtMidiPortIndex(L"loopMIDI Port 1") == L"loopMIDI Port", "the port index comes off the name");
    Require(StripRtMidiPortIndex(L"Digital Piano 2 3") == L"Digital Piano 2", "only the last number is RtMidi's");
    Require(StripRtMidiPortIndex(L"MIDI") == L"MIDI", "a name without a suffix is unchanged");
    Require(StripRtMidiPortIndex(L"88") == L"88", "a name that is only digits is not a suffix");
    Require(StripRtMidiPortIndex(L"") == L"", "an empty name survives");

    Require(ResolveWinMMPort(L"winmm:1|loopMIDI Port", present) == 1, "an id opens the port it names");
    Require(ResolveWinMMPort(L"winmm:0|MIDI", present) == 0, "and so does the other one");

    // The case the ids were introduced for. Unplugging the first device shifts
    // the second down, and RtMidi renames it as it goes.
    const std::vector<std::wstring> renumbered{L"loopMIDI Port 0"};
    Require(ResolveWinMMPort(L"winmm:1|loopMIDI Port", renumbered) == 0,
            "renumbering does not lose the device, even though its name changed with it");

    // The bug worth having a test for: the wanted device is gone and the other
    // one is still there. Opening that instead is silent and wrong.
    Require(ResolveWinMMPort(L"winmm:0|MIDI", renumbered) == -1,
            "a device that is not present is not substituted with one that is");
    Require(ResolveWinMMPort(L"winmm:0|MIDI", {}) == -1, "no ports at all is not port zero");

    // Two keyboards of the same model report the same name, which is what
    // RtMidi's suffix was for. The index is the tie-break, not the answer.
    const std::vector<std::wstring> twins{L"Digital Piano 0", L"Digital Piano 1"};
    Require(ResolveWinMMPort(L"winmm:1|Digital Piano", twins) == 1, "the index picks between identical names");
    Require(ResolveWinMMPort(L"winmm:0|Digital Piano", twins) == 0, "and picks the other one when asked");
    // With one of the twins gone the index no longer means anything, so the
    // remaining one is the only honest answer.
    Require(ResolveWinMMPort(L"winmm:1|Digital Piano", {L"Digital Piano 0"}) == 0,
            "a stale index falls back to the name, not to nothing");

    // Ids from before names were recorded, and ids that are not ours at all.
    Require(ResolveWinMMPort(L"winmm:1", present) == 1, "an id with only an index still opens that port");
    Require(ResolveWinMMPort(L"winmm:9", present) == -1, "an index past the end is not clamped into range");
    Require(ResolveWinMMPort(L"", present) == -1, "an empty id names nothing");
    Require(ResolveWinMMPort(L"\\\\?\\SWD#MMDEVAPI#MIDIU_KSA", present) == -1, "a WinRT id is not a WinMM id");
    Require(ResolveWinMMPort(L"wooting:analog", present) == -1, "a Wooting id is not a WinMM id");
    Require(ResolveWinMMPort(L"winmm:x|MIDI", present) == -1, "a malformed index is refused, not read as zero");

    // A name carrying the separator keeps every character of it.
    Require(ResolveWinMMPort(L"winmm:0|A|B", {L"A|B 0"}) == 0, "a bar inside a name is part of the name");

    // The machine this runs on. Every id the enumerator hands out has to open
    // the row it came from, and no two rows may land on the same port. With
    // two ports present that is exactly the case ids were introduced for, so
    // it is checked rather than assumed.
    auto winmm = CreateMidiInput(MidiBackend::WinMM);
    const auto rows = winmm->enumerate();
    std::vector<std::wstring> live;
    for (const auto& row : rows) live.push_back(row.name);
    std::set<int> taken;
    for (const auto& row : rows) {
        const int port = ResolveWinMMPort(row.id, live);
        Require(port >= 0 && static_cast<size_t>(port) < live.size(), "an enumerated port must resolve to itself");
        Require(taken.insert(port).second, "two rows must never resolve to the same port");
    }
    // Grouping, asked for by the panel seat so the device list can show one
    // piano with a choice of transport instead of three near-identical rows.
    // The answer was already being computed to decide the "(WinMM)" suffix and
    // then discarded, which is why a tester had to hunt for their keyboard.
    const auto listed = EnumerateMidiInputs();
    for (const auto& device : listed) {
        Require(!device.group.empty(), "every row says which device it is");
        Require(device.name.rfind(device.group, 0) == 0,
                "the group is the display name before any transport suffix");
    }
    std::map<std::wstring, std::set<int>> byGroup;
    for (const auto& device : listed) byGroup[device.group].insert(static_cast<int>(device.backend));
    for (const auto& [group, backends] : byGroup)
        Require(backends.size() <= 4, "a group is one socket, not an accumulation of unrelated rows");
    std::cout << "PASS WinMM port resolution across renumbering, duplicates and absent devices ("
              << listed.size() << " rows in " << byGroup.size() << " device groups)\n";
}

// Two devices present at once has never actually been confirmed, only designed
// for. It needs two inputs, so it runs when this machine has them and says so
// when it does not: the suite as a whole must not require MIDI hardware.
// Two loopMIDI ports reproduce it without a second piano.
void TwoDeviceTests() {
    std::vector<MidiInputDevice> ports;
    for (const auto& device : EnumerateMidiInputs())
        if (device.backend != MidiBackend::WootingAnalog) ports.push_back(device);

    if (ports.size() < 2) {
        std::cout << "SKIP two MIDI devices at once: " << ports.size()
                  << " input present, needs 2 (create a second loopMIDI port)\n";
        return;
    }

    const auto silent = [](uint64_t, const uint8_t*, size_t) {};
    std::set<std::wstring> ids;
    for (const auto& device : ports) {
        Require(ids.insert(device.id).second, "every enumerated device needs its own id");
        auto input = CreateMidiInput(device.backend);
        Require(input->open(device.id, silent), "an enumerated device must open by its own id");
        Require(input->openedDeviceId() == device.id, "the port opened is the port that was asked for");
        input->close();
        Require(!input->isOpen(), "closing releases the port");
    }

    // The case three disagreeing index spaces used to get wrong: both open,
    // at the same time, each on the device it was given.
    auto first = CreateMidiInput(ports[0].backend);
    auto second = CreateMidiInput(ports[1].backend);
    Require(first->open(ports[0].id, silent), "the first of two opens");
    Require(second->open(ports[1].id, silent), "the second opens alongside it rather than replacing it");
    Require(first->isOpen() && second->isOpen(), "both stay open");
    Require(first->openedDeviceId() == ports[0].id && second->openedDeviceId() == ports[1].id,
            "two open devices are not the same device twice");
    first->close();
    Require(second->isOpen(), "closing one device leaves the other alone");
    second->close();

    std::cout << "PASS two MIDI devices open at once, each on the port it was given ("
              << ports.size() << " inputs present)\n";
}

// The three controls wooting-analog-midi exposes and this backend did not.
// Their absence is why a Wooting here played one fixed layout of white keys at
// one fixed sensitivity, so the defaults are that app's and a number carried
// over from it has to mean the same thing.
void WootingSettingsTests() {
    const auto defaults = midi::WootingAnalogSettings{};
    Require(defaults.TRIGGER_THRESHOLD == 0.5 && defaults.SHIFT_AMOUNT == 12 && defaults.VELOCITY_SCALE == 5.0,
            "defaults match wooting-analog-midi");

    WootingAnalogSettings applied{0.25f, 0.5f, 1, 2.0f};
    SetWootingAnalogSettings(applied);
    const auto read = GetWootingAnalogSettings();
    Require(read.trigger == 0.25f && read.shiftAmount == 1 && read.velocityScale == 2.0f,
            "settings survive the round trip into the backend");
    SetWootingAnalogSettings({});

    // Upstream's formula: rate * scale / 100, clamped, onto 1..127. At the
    // default scale of 5, twenty units of depth per second is a full strike.
    Require(WootingVelocityFor(0.2f, 0.0f, 0.01, 5.0f) == 127, "a fast strike reaches full velocity");
    Require(WootingVelocityFor(0.1f, 0.0f, 0.01, 5.0f) == 64, "half that rate is half the range");
    Require(WootingVelocityFor(0.1f, 0.0f, 0.01, 10.0f) == 127, "doubling the scale doubles the reading");
    Require(WootingVelocityFor(0.1f, 0.0f, 0.01, 2.5f) == 32, "halving the scale halves it");
    // A key on the way back up is not a strike, and no elapsed time is no
    // measurement at all rather than a silent or a maximum note.
    Require(WootingVelocityFor(0.0f, 0.5f, 0.01, 5.0f) == 1, "a key travelling back up is not a strike");
    Require(WootingVelocityFor(0.5f, 0.0f, 0.0, 5.0f) == 96, "no elapsed time answers in the middle");

    // Every field is optional so a config naming only what the user changed
    // still loads, and a config written before this existed keeps the defaults.
    midi::WootingAnalogSettings parsed;
    nlohmann::json partial = {{"SHIFT_AMOUNT", 1}};
    partial.get_to(parsed);
    Require(parsed.SHIFT_AMOUNT == 1 && parsed.TRIGGER_THRESHOLD == 0.5 && parsed.VELOCITY_SCALE == 5.0,
            "a partial block changes only what it names");

    const auto rejects = [](const nlohmann::json& value) {
        midi::WootingAnalogSettings out;
        try { value.get_to(out); } catch (const midi::ConfigException&) { return true; }
        return false;
    };
    Require(rejects({{"TRIGGER_THRESHOLD", 0.0}}), "a zero trigger would fire on a resting key");
    Require(rejects({{"TRIGGER_THRESHOLD", 1.5}}), "a trigger past full travel could never fire");
    Require(rejects({{"RELEASE_FRACTION", 1.0}}), "a release at the trigger leaves no gap to stop stutter");
    Require(rejects({{"SHIFT_AMOUNT", 200}}), "a shift wider than the MIDI range is a typo");
    Require(rejects({{"VELOCITY_SCALE", 0.0}}), "a zero scale would silence every note");
    std::cout << "PASS wooting trigger, shift amount and velocity scale\n";
}

// The velocity graph's missing half. observe() carries the decode, so the one
// line inside MIDI2Key::ProcessMidiMessage that calls it has nothing left to
// get wrong; these drive the same bytes a MIDI callback would.
void VelocityTelemetryTests() {
    namespace vt = velocity_telemetry;
    vt::reset();
    auto empty = vt::snapshot();
    Require(empty.total == 0 && empty.last == 0, "reset clears the histogram and the live velocity");

    const uint8_t noteOn[3]{0x90, 60, 100};
    vt::observe(noteOn, sizeof(noteOn));
    auto one = vt::snapshot();
    Require(one.total == 1 && one.last == 100, "a note on is recorded with its velocity");
    Require(one.buckets[vt::bucketFor(100)] == 1, "the note lands in its own bucket");
    Require(one.revision > empty.revision, "recording moves the revision");

    // Everything that is not a sounding note on has to leave the graph alone,
    // or the histogram fills with events the player never played.
    const uint8_t noteOffZero[3]{0x90, 60, 0};      // note on, velocity 0
    const uint8_t noteOff[3]{0x80, 60, 64};         // real note off, and its velocity is a release
    const uint8_t sustain[3]{0xB0, 64, 127};        // control change
    const uint8_t truncated[2]{0x90, 60};
    vt::observe(noteOffZero, sizeof(noteOffZero));
    vt::observe(noteOff, sizeof(noteOff));
    vt::observe(sustain, sizeof(sustain));
    vt::observe(truncated, sizeof(truncated));
    vt::observe(nullptr, 3);
    auto still = vt::snapshot();
    Require(still.total == 1 && still.last == 100,
            "note offs, control change, short buffers and null leave the histogram alone");

    // Every channel reaches the same histogram. MIDI2Key filters by channel
    // before it calls in, so a per-channel filter here would apply it twice.
    const uint8_t otherChannel[3]{0x95, 60, 100};
    vt::observe(otherChannel, sizeof(otherChannel));
    Require(vt::snapshot().total == 2, "channel selection belongs to the caller");

    // 1 and 127 are the ends of the real range and must not fall outside the
    // array. 128 cannot arrive from a valid message and must not be trusted to.
    vt::reset();
    for (int velocity = 1; velocity <= 127; ++velocity) vt::record(static_cast<uint8_t>(velocity));
    vt::record(128);
    auto full = vt::snapshot();
    Require(full.total == 127 && full.last == 127, "the whole velocity range is counted and 128 is refused");
    Require(vt::bucketFor(1) == 0 && vt::bucketFor(127) == vt::kBuckets - 1, "the ends map to the end buckets");
    uint32_t counted = 0;
    for (int i = 0; i < vt::kBuckets; ++i) {
        Require(full.buckets[i] > 0, "every bucket is reachable from a real velocity");
        counted += full.buckets[i];
    }
    Require(counted == full.total, "the buckets and the total agree");

    // record() runs on the MIDI callback thread while the UI reads at frame
    // rate. Nothing may be lost, and the reader may not tear into a crash.
    vt::reset();
    constexpr int writers = 4, each = 20000;
    std::atomic<bool> go{false}, stop{false};
    std::vector<std::thread> threads;
    for (int w = 0; w < writers; ++w) threads.emplace_back([&, w] {
        while (!go.load()) std::this_thread::yield();
        for (int n = 0; n < each; ++n) vt::record(static_cast<uint8_t>(1 + (w * 31 + n) % 127));
    });
    std::atomic<uint64_t> reads{0};
    std::thread reader([&] {
        while (!stop.load()) { auto s = vt::snapshot(); if (s.total <= writers * each) reads.fetch_add(1); }
    });
    go.store(true);
    for (auto& thread : threads) thread.join();
    stop.store(true);
    reader.join();
    auto raced = vt::snapshot();
    Require(raced.total == writers * each, "concurrent recording loses nothing");
    Require(reads.load() > 0, "the reader kept up while notes arrived");
    vt::reset();
    std::cout << "PASS velocity telemetry: decode, range, bucket ends and concurrent recording\n";
}
}

// Queue items 1 and 2, defended rather than merely tested once.
//
// The panel seat verified both when it built them, in two throwaway programs
// under build/parity-qa that included this file with wmain macro-renamed and
// reached MIDI2Key::ProcessMidiMessage through "#define private public". They
// proved the features work. They also lived in a gitignored build directory,
// ran in no suite, and would have been deleted by the next clean, so nothing
// they asserted could ever fail again. Ported here, which is what makes them a
// gate instead of a receipt.
//
// The private-access hack is gone: MIDI2Key is driven through a substituted
// transport, which is a seam the MIDI output work needs anyway.

// A transport that delivers whatever the test hands it.
class FakeMidiInput final : public IMidiInput {
public:
    bool allowOpen = true;
    MidiBackend backend() const noexcept override { return MidiBackend::WinMM; }
    std::vector<MidiInputDevice> enumerate() override {
        return {{L"fake:0|Test piano", L"Test piano", MidiBackend::WinMM}};
    }
    bool open(const std::wstring& deviceId, MidiInputCallback callback) override {
        if (!allowOpen) return false;
        opened_ = deviceId;
        callback_ = std::move(callback);
        return true;
    }
    void close() override { callback_ = nullptr; opened_.clear(); }
    bool isOpen() const noexcept override { return static_cast<bool>(callback_); }
    const std::wstring& openedDeviceId() const noexcept override { return opened_; }
    void Deliver(std::initializer_list<uint8_t> message) {
        if (callback_) callback_(0, message.begin(), message.size());
    }
private:
    MidiInputCallback callback_;
    std::wstring opened_;
};

int ArrowPresses(const std::vector<Captured>& events) {
    int count = 0;
    for (const auto& event : events)
        if (IsNotePress(event) && (event.input.ki.wScan == 0x4b || event.input.ki.wScan == 0x4d)) ++count;
    return count;
}

// The 61-key layout was unreachable: ShellEngine pinned eightyEightKeyModeActive
// on for every player it built, so anyone on a 61-key game piano got the 88-key
// map. What has to hold now is not only that the switch exists, but that
// switching is safe: a key held under the outgoing layout comes up before the
// incoming one can type anything, and the two layouts keep separate bindings.
void LayoutTests(const std::filesystem::path& directory) {
    const auto config = directory / L"layout.json";
    const auto fixture = directory / L"layout.mid";
    nlohmann::json settings;
    { std::ifstream input(directory / L"config.json"); input >> settings; }
    // Distinct characters per layout, so which map dispatched is visible in the
    // scancode rather than inferred.
    settings["KEY_MAPPINGS"]["FULL"]["C4"] = "a";     // 0x1e
    settings["KEY_MAPPINGS"]["LIMITED"]["C4"] = "b";  // 0x30
    settings["KEY_MAPPINGS"]["LIMITED"]["C5"] = "d";  // 0x20
    settings["SHELL_88_KEYS"] = true;
    { std::ofstream output(config); output << settings; }
    WriteTrackFixture(fixture);

    const auto caller = GetCurrentThreadId();
    using A = shell::ShellEngine::Action;
    {
        shell::ShellEngine engine(config);
        engine.Send({A::Load, fixture});
        Await([&] { return !engine.Snapshot()->loaded.empty(); }, "the fixture never loaded");
        const auto generation = engine.Snapshot()->generation;
        engine.Send({A::Solo, {}, generation, 1, true});
        engine.Send({A::Play, {}, generation});
        Await([&] {
            std::lock_guard lock(capturedMutex);
            return std::any_of(captured.begin(), captured.end(),
                [](const Captured& e) { return IsNotePress(e) && e.input.ki.wScan == 0x1e; });
        }, "the 88-key layout never attacked its note");

        engine.Send({A::EightyEightKeys, {}, 0, 0, false});
        Await([&] { return !engine.Snapshot()->eightyEightKeys; }, "the layout never switched");
        Require(!engine.Snapshot()->playing, "switching layout pauses playback");
        const auto released = TakeCaptured();
        Require(std::any_of(released.begin(), released.end(), [](const Captured& e) {
            return e.input.ki.wScan == 0x1e && (e.input.ki.dwFlags & KEYEVENTF_KEYUP); }),
            "a key held under the outgoing layout must be released by the switch");
        for (const auto& event : released)
            Require(event.thread != caller, "the release stayed off the calling thread");

        const auto playExpecting = [&](WORD expected, const char* what) {
            TakeCaptured();
            engine.Send({A::Restart, {}, generation});
            engine.Send({A::Play, {}, generation});
            Await([&] { return engine.Snapshot()->playing; }, "playback never started");
            Await([&] { return !engine.Snapshot()->playing; }, "playback never finished");
            int attacks = 0;
            for (const auto& event : TakeCaptured()) {
                Require(event.thread != caller, "dispatch stayed off the calling thread");
                if (IsNotePress(event)) { ++attacks; Require(event.input.ki.wScan == expected, what); }
            }
            Require(attacks == 1, "the soloed part is one note");
        };
        playExpecting(0x30, "the 61-key layout types its own binding, not the 88-key one");

        engine.Send({A::Remap, {}, 0, 60, false, 0, "c"});
        Await([&] { return engine.Snapshot()->keyMappings.at("C4") == "c"; }, "the 61-key remap never applied");
        playExpecting(0x2e, "a remap under 61 keys reaches dispatch");

        engine.Send({A::Transpose, {}, generation, 0, false, 12});
        Await([&] { return engine.Snapshot()->transpose == 12; }, "transpose never applied");
        playExpecting(0x20, "transpose resolves against the selected layout");

        engine.Send({A::EightyEightKeys, {}, 0, 0, true});
        Await([&] { return engine.Snapshot()->eightyEightKeys; }, "the layout never switched back");
        Require(engine.Snapshot()->keyMappings.at("C4") == "a",
                "remapping one layout must not edit the other");
        engine.Send({A::EightyEightKeys, {}, 0, 0, false});
        Await([&] { return !engine.Snapshot()->eightyEightKeys; }, "the layout never switched again");
    }
    {
        shell::ShellEngine engine(config);
        Await([&] { return !engine.Snapshot()->curves.empty(); }, "the engine never came up");
        Require(!engine.Snapshot()->eightyEightKeys, "the layout choice survives a restart");
        Require(engine.Snapshot()->keyMappings.at("C4") == "c", "and so does its own binding");

        // The snapshot agreeing is not the same as dispatch agreeing, and this
        // is the path the pin actually lived on. ensurePlayer builds the player
        // once, on first use, and the switch action sets the flag again on its
        // way through, so a test that switches and then plays proves nothing
        // about a session that never switches. Restoring the old
        // `= true` pin left every assertion above passing.
        //
        // This is that session: 61 keys chosen last time, app restarted, play
        // pressed, nothing touched in between.
        engine.Send({A::Load, fixture});
        Await([&] { return !engine.Snapshot()->loaded.empty(); }, "the fixture never loaded");
        const auto generation = engine.Snapshot()->generation;
        engine.Send({A::Solo, {}, generation, 1, true});
        TakeCaptured();
        engine.Send({A::Play, {}, generation});
        Await([&] { return engine.Snapshot()->playing; }, "playback never started");
        Await([&] { return !engine.Snapshot()->playing; }, "playback never finished");
        int attacks = 0;
        for (const auto& event : TakeCaptured())
            if (IsNotePress(event)) {
                ++attacks;
                Require(event.input.ki.wScan == 0x2e,
                        "a restarted session dispatches the saved layout, not the 88-key map");
            }
        Require(attacks == 1, "the soloed part is one note");
    }
    std::cout << "PASS layout switch: held release, selected dispatch, transpose, separate bindings, restart dispatch\n";
}

// AutoVol drives the game's volume by typing arrows at it, so the failure that
// matters is not that it does nothing, it is that it does something without
// being asked: 59 keystrokes into whatever window had focus. Every assertion
// below is about when the sweep must NOT happen.
void AutoVolumeTests(const std::filesystem::path& config, const std::filesystem::path& fixture) {
    struct WindowHost : shell::AutoVolumeHost {
        shell::GameWindow target{1, 2, "Test game"};
        bool exists = true, focusWorks = true, foreground = true;
        std::atomic<int> focused{0};
        std::vector<shell::GameWindow> Windows() override {
            return exists ? std::vector{target} : std::vector<shell::GameWindow>{};
        }
        bool Focus(const shell::GameWindow&) override { ++focused; return focusWorks; }
        bool IsForeground(const shell::GameWindow&) override { return foreground; }
    };

    // Live input honours the flag, driven through a substituted transport
    // rather than through MIDI2Key's private members.
    {
        auto* fake = new FakeMidiInput();
        SetMidiInputFactory([fake](MidiBackend) {
            return std::unique_ptr<IMidiInput>(fake);   // one open, one test
        });
        struct Restore { ~Restore() { SetMidiInputFactory({}); } } restore;

        VirtualPianoPlayer player(false, config);
        player.eightyEightKeyModeActive = true;
        MIDI2Key live(&player);
        player.toggle_volume_adjustment();
        live.SetActive(true);
        live.OpenDevice(L"winmm:0|Test piano");
        Require(!live.GetSelectedDevice().empty(), "the substituted transport opened");

        TakeCaptured();
        fake->Deliver({0x90, 60, 127});
        Require(ArrowPresses(TakeCaptured()) > 0, "live input adjusts volume while AutoVol is on");
        fake->Deliver({0x80, 60, 0});

        player.toggle_volume_adjustment();
        TakeCaptured();
        fake->Deliver({0x90, 60, 1});
        Require(ArrowPresses(TakeCaptured()) == 0, "and sends no arrows at all while it is off");
        fake->Deliver({0x80, 60, 0});
        live.SetActive(false);
        live.CloseDevice();
    }

    auto host = std::make_shared<WindowHost>();
    shell::ShellEngine engine(config, host);
    using A = shell::ShellEngine::Action;
    Await([&] { return !engine.Snapshot()->curves.empty(); }, "the engine never came up");
    Require(!engine.Snapshot()->autoVolume, "AutoVol is off until it is asked for");

    const auto arm = [&] {
        shell::ShellEngine::Command command{A::AutoVolumeCalibrate, {}, engine.Snapshot()->generation};
        command.window = host->target;
        engine.Send(command);
        Await([&] { return engine.Snapshot()->autoVolumeCountdown == 3; }, "the countdown never armed");
    };

    TakeCaptured();
    arm();
    Require(ArrowPresses(TakeCaptured()) == 0 && host->focused == 0,
            "arming the countdown neither focuses nor types");
    engine.Send({A::AutoVolumeCancel});
    Await([&] { return !engine.Snapshot()->autoVolumeCountdown; }, "cancel never took");
    std::this_thread::sleep_for(3100ms);
    Require(ArrowPresses(TakeCaptured()) == 0 && host->focused == 0,
            "a cancelled calibration never runs, however long you wait");

    arm();
    Await([&] { return engine.Snapshot()->autoVolume; }, "calibration never completed");
    const auto sweep = TakeCaptured();
    // 50 down then 9 up, from the default INITIAL_VOLUME and VOLUME_STEP. The
    // count is the assertion: toggle_volume_adjustment() already calibrates,
    // and the original calls calibrate_volume() again straight after, so a
    // shell that copied the original would show 118 here.
    Require(ArrowPresses(sweep) == 59, "exactly one sweep, not the original's two");
    Require(host->focused == 1, "one focus request");
    for (const auto& event : sweep)
        Require(event.thread != GetCurrentThreadId(), "calibration is worker owned");

    engine.Send({A::Load, fixture});
    Await([&] { return !engine.Snapshot()->loaded.empty(); }, "the fixture never loaded");
    Require(!engine.Snapshot()->autoVolume && engine.Snapshot()->autoVolumeNeedsCalibration,
            "loading a file invalidates the calibration rather than trusting it");
    Require(ArrowPresses(TakeCaptured()) == 0 && host->focused == 1,
            "and never silently recalibrates, which is what the original did");

    arm();
    Await([&] { return engine.Snapshot()->autoVolume; }, "recalibration never completed");
    TakeCaptured();
    const auto generation = engine.Snapshot()->generation;
    engine.Send({A::Solo, {}, generation, 1, true});
    engine.Send({A::Play, {}, generation});
    Await([&] { return engine.Snapshot()->playing; }, "playback never started");
    Await([&] { return !engine.Snapshot()->playing; }, "playback never finished");
    Require(ArrowPresses(TakeCaptured()) > 0, "autoplay adjusts volume from velocity");

    engine.Send({A::AutoVolumeOff});
    Await([&] { return !engine.Snapshot()->autoVolume && !engine.Snapshot()->autoVolumeNeedsCalibration; },
          "AutoVol never switched off");

    host->focusWorks = false;
    arm();
    Await([&] { return !engine.Snapshot()->error.empty(); }, "a focus failure was never reported");
    Require(!engine.Snapshot()->autoVolume && ArrowPresses(TakeCaptured()) == 0,
            "a window that cannot be focused gets no keystrokes");

    host->focusWorks = true;
    host->foreground = false;
    arm();
    Await([&] { return !engine.Snapshot()->error.empty(); }, "a foreground mismatch was never reported");
    Require(!engine.Snapshot()->autoVolume && ArrowPresses(TakeCaptured()) == 0,
            "a window that did not come forward gets no keystrokes either");

    host->foreground = true;
    arm();
    engine.Send({A::Stop});
    Await([&] { return !engine.Snapshot()->autoVolumeCountdown; }, "Stop never cancelled the countdown");
    std::this_thread::sleep_for(3100ms);
    Require(ArrowPresses(TakeCaptured()) == 0, "Stop prevents a calibration that was counting down");

    std::cout << "PASS AutoVol: countdown, cancel, Stop, focus failure, one sweep, reload, autoplay\n";
}

void DeviceGroupingTests() {
    using namespace shell;
    std::vector<LiveDevice> inputs{
        {L"rt:piano", "Piano", L"Piano", MidiBackend::WinRT},
        {L"mm:piano", "Piano", L"Piano", MidiBackend::WinMM},
        {L"ks:piano", "Piano", L"Piano", MidiBackend::KernelStreaming},
        {L"rt:loop", "Loop", L"Loop", MidiBackend::WinRT}};
    const auto groups = GroupDevices(inputs);
    Require(groups.size() == 2 && groups[0].inputs.size() == 3, "one piano is one row with three transports");
    Require(PreferredInput(groups[0], {}) == L"ks:piano", "new device selection prefers Kernel Streaming when present");
    Require(PreferredInput(groups[0], L"mm:piano") == L"mm:piano", "reselection retains the chosen transport");
    Require(SelectedGroup(groups, L"rt:loop") == &groups[1], "transport selection belongs to the selected device");
    inputs.push_back({L"rt:second", "Piano", L"Piano", MidiBackend::WinRT});
    const auto separate = GroupDevices(inputs);
    Require(separate.size() == 5, "same-name devices must fall back to individual ids, not merge keyboards");
    std::set<std::wstring> ids;
    for (const auto& group : separate) {
        Require(group.inputs.size() == 1, "ambiguous names cannot offer another keyboard as a transport");
        ids.insert(PreferredInput(group, {}));
    }
    Require(ids.size() == inputs.size(), "every ambiguous port stays independently selectable");
    Require(GroupDevices({{L"one", "Same"}, {L"two", "Same"}}).size() == 2, "missing group metadata falls back to ids");
    std::cout << "PASS device grouping, transport retention and same-name id fallback\n";
}

void ShellLogTests(const std::filesystem::path& config) {
    auto& log = shell::ShellLog::Instance();
    log.Clear();
    auto* oldOut = std::cout.rdbuf(); auto* oldErr = std::cerr.rdbuf();
    auto* oldWide = std::wcerr.rdbuf(); auto* oldClog = std::clog.rdbuf();
    {
        shell::CaptureShellLog capture;
        std::cout << "output line\n";
        std::cerr << "read failed\n";
        std::wcerr << L"Kernel Streaming \u97f3\u4e50 read failed\n";
        std::wcout << L"wide output\n";
        std::clog << "diagnostic\n"; std::wclog << L"wide diagnostic\n";
        std::cout << "partial";
        const auto first = log.Snapshot();
        Require(first->find("partial") != std::string::npos, "partial messages are visible without a newline");
        Require(first->find("[error] read failed") != std::string::npos, "stderr reaches the visible log");
        Require(first->find("Kernel Streaming \xe9\x9f\xb3\xe4\xb9\x90 read failed") != std::string::npos, "wide KS errors preserve Unicode");
        Require(first->find("wide output") != std::string::npos && first->find("wide diagnostic") != std::string::npos,
                "wide and diagnostic streams are captured");
        std::vector<std::thread> writers;
        for (int i = 0; i < 4; ++i) writers.emplace_back([] { for (int j = 0; j < 100; ++j) std::cout << "writer line\n"; });
        for (auto& writer : writers) writer.join();
        const auto concurrent = log.Snapshot();
        size_t count = 0, pos = 0;
        while ((pos = concurrent->find("writer line", pos)) != std::string::npos) { ++count; ++pos; }
        Require(count == 400, "concurrent log writers lose no messages");
        Require(first->find("writer line") == std::string::npos, "published log snapshots remain immutable");
        std::cout << std::string(shell::ShellLog::Capacity + 100, 'x');
        Require(log.Snapshot()->size() <= shell::ShellLog::Capacity, "log history is bounded");
        shell::ShellEngine engine(config);
        Await([&] { return !engine.Snapshot()->curves.empty(); }, "log test engine did not initialize");
        engine.Send({shell::ShellEngine::Action::ClearLog});
        Await([&] { return engine.Snapshot()->log->empty(); }, "Clear Log did not clear the engine snapshot");
        std::wcerr << L"idle callback error\n";
        Await([&] { return engine.Snapshot()->log->find("idle callback error") != std::string::npos; },
              "log snapshot did not refresh while the engine worker slept");
    }
    Require(std::cout.rdbuf() == oldOut && std::cerr.rdbuf() == oldErr && std::wcerr.rdbuf() == oldWide &&
        std::clog.rdbuf() == oldClog, "capture restores the host streams");
    log.Clear();
    std::cout << "PASS shell log: wide errors, partial messages, concurrency, retention, idle refresh and Clear Log\n";
}

void WriteHeldNoteFixture(const std::filesystem::path& path, uint8_t note) {
    const std::vector<uint8_t> bytes{'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,
        'M','T','r','k',0,0,0,13,0,0x90,note,80,0x8f,0,0x80,note,0,0,0xff,0x2f,0};
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool HasKey(const std::vector<Captured>& events, WORD scan, bool down) {
    return std::any_of(events.begin(), events.end(), [&](const Captured& e) {
        return e.input.ki.wScan == scan && ((e.input.ki.dwFlags & KEYEVENTF_KEYUP) == 0) == down;
    });
}
bool OnlyModifierReleases(const std::vector<Captured>& events) {
    return std::all_of(events.begin(), events.end(), [](const Captured& event) {
        return (event.input.ki.dwFlags & KEYEVENTF_KEYUP) &&
            (event.input.ki.wVk == VK_MENU || event.input.ki.wVk == VK_CONTROL);
    });
}
void AwaitKey(WORD scan, bool down, const char* error) {
    Await([&] { std::lock_guard lock(capturedMutex); return HasKey(captured, scan, down); }, error);
}

void OutRangeSwitchTests(const std::filesystem::path& directory) {
    using A = shell::ShellEngine::Action;
    const auto config = directory / L"out-range-switch.json", fixture = directory / L"held-low.mid";
    nlohmann::json settings;
    { std::ifstream file(directory / L"config.json"); file >> settings; }
    settings["SHELL_88_KEYS"] = false;
    settings["SHELL_OUT_RANGE"] = true;
    settings["KEY_MAPPINGS"]["LIMITED"]["A2"] = "a";
    { std::ofstream file(config); file << settings; }
    WriteHeldNoteFixture(fixture, 21);
    FakeMidiInput* input = nullptr;
    SetMidiInputFactory([&](MidiBackend) { auto fake = std::make_unique<FakeMidiInput>(); input = fake.get(); return fake; });
    struct Restore { ~Restore() { SetMidiInputFactory({}); } } restore;
    const auto caller = GetCurrentThreadId();
    {
        shell::ShellEngine engine(config);
        engine.Send({A::Load, fixture});
        Await([&] { return engine.Snapshot()->loaded == fixture; }, "OutRange fixture did not load");
        const auto generation = engine.Snapshot()->generation;
        Require(engine.Snapshot()->outRange, "OutRange choice was not restored");
        TakeCaptured();
        engine.Send({A::Play, {}, generation});
        AwaitKey(0x1e, true, "OutRange autoplay did not fold A0 to the A2 binding");
        TakeCaptured();
        engine.Send({A::OutRange, {}, 0, 0, false});
        Await([&] { return !engine.Snapshot()->outRange; }, "OutRange did not switch off");
        const auto releases = TakeCaptured();
        Require(HasKey(releases, 0x1e, false), "OutRange switch left the folded autoplay key held");
        Require(!engine.Snapshot()->playing, "OutRange switch must pause autoplay before changing the fold");
        for (const auto& event : releases) Require(event.thread != caller, "OutRange release ran on the UI thread");
        engine.Send({A::OutRange, {}, 0, 0, true});
        shell::ShellEngine::Command open{A::LiveOpen}; open.device = L"winmm:0|Test piano"; engine.Send(open);
        Await([&] { return engine.Snapshot()->liveActive; }, "live OutRange input did not open");
        TakeCaptured(); input->Deliver({0x90, 21, 80});
        Require(HasKey(TakeCaptured(), 0x1e, true), "live OutRange did not fold the low note");
        engine.Send({A::OutRange, {}, 0, 0, false});
        Await([&] { return !engine.Snapshot()->outRange; }, "live OutRange switch did not finish");
        Require(engine.Snapshot()->liveActive, "live input should reopen after changing OutRange");
        const auto liveRelease = TakeCaptured();
        Require(HasKey(liveRelease, 0x1e, false), "OutRange switch left the folded live key held");
        for (const auto& event : liveRelease) Require(event.thread != caller, "live OutRange release ran on the UI thread");
        input->Deliver({0x90, 21, 80});
        Require(!HasKey(TakeCaptured(), 0x1e, true), "disabled OutRange still folds live input");
        engine.Send({A::Stop});
        Await([&] { return !engine.Snapshot()->liveActive; }, "Stop did not disable live input");
    }
    {
        shell::ShellEngine engine(config);
        Await([&] { return !engine.Snapshot()->curves.empty(); }, "OutRange restart did not initialize");
        Require(!engine.Snapshot()->outRange, "OutRange switch did not persist");
    }
    std::cout << "PASS OutRange switch: folded autoplay and live keys released before remapping, off-UI dispatch, persistence\n";
}

void CountdownTests(const std::filesystem::path& directory) {
    using A = shell::ShellEngine::Action;
    const auto config = directory / L"countdown.json", fixture = directory / L"countdown.mid";
    nlohmann::json settings;
    { std::ifstream file(directory / L"config.json"); file >> settings; }
    settings["SHELL_PLAYBACK_DELAY"] = 1;
    settings["KEY_MAPPINGS"]["FULL"]["C4"] = "a";
    settings["SHELL_88_KEYS"] = true;
    { std::ofstream file(config); file << settings; }
    WriteHeldNoteFixture(fixture, 60);
    shell::ShellEngine engine(config, {}, true);
    engine.Send({A::Load, fixture});
    Await([&] { return engine.Snapshot()->loaded == fixture; }, "countdown fixture did not load");
    auto generation = engine.Snapshot()->generation;
    TakeCaptured();
    engine.Send({A::Play, {}, generation});
    Await([&] { return !engine.Snapshot()->error.empty(); }, "unacknowledged autoplay was not rejected");
    Require(!HasKey(TakeCaptured(), 0x1e, true), "typing warning gate allowed an autoplay note");
    engine.Send({A::AcknowledgeTyping});
    Await([&] { return engine.Snapshot()->typingAcknowledged; }, "warning acknowledgment did not apply");
    TakeCaptured();
    auto armed = std::chrono::steady_clock::now();
    engine.Send({A::PlayCountdown, {}, generation});
    Await([&] { return engine.Snapshot()->playbackCountdown == 1; }, "mouse Play did not arm the countdown");
    std::this_thread::sleep_for(150ms);
    Require(!HasKey(TakeCaptured(), 0x1e, true), "countdown typed a note before expiry");
    AwaitKey(0x1e, true, "countdown expired without starting playback");
    Require(std::chrono::steady_clock::now() - armed >= 950ms, "countdown started early");
    engine.Send({A::Stop});
    Await([&] { return !engine.Snapshot()->playing; }, "countdown playback did not stop");
    for (const auto cancel : {A::PlayCountdown, A::TogglePlayPause, A::Stop, A::Seek, A::Restart, A::Load}) {
        TakeCaptured();
        generation = engine.Snapshot()->generation;
        engine.Send({A::PlayCountdown, {}, generation});
        Await([&] { return engine.Snapshot()->playbackCountdown == 1; }, "countdown did not rearm");
        engine.Send({cancel, cancel == A::Load ? fixture : std::filesystem::path{}, generation});
        Await([&] { return engine.Snapshot()->playbackCountdown == 0; }, "transport action did not cancel countdown");
        std::this_thread::sleep_for(1050ms);
        Require(!HasKey(TakeCaptured(), 0x1e, true), "cancelled countdown still typed a note");
    }
    generation = engine.Snapshot()->generation;
    engine.Send({A::PlayCountdown, {}, generation - 1});
    engine.Send({A::PlaybackDelay, {}, 0, 0, false, 0});
    Await([&] { return engine.Snapshot()->playbackDelay == 0; }, "zero countdown did not apply");
    Require(engine.Snapshot()->playbackCountdown == 0, "a stale generation armed a countdown");
    TakeCaptured(); engine.Send({A::PlayCountdown, {}, generation});
    AwaitKey(0x1e, true, "zero-delay mouse Play did not start");
    engine.Send({A::Stop});
    Await([&] { return !engine.Snapshot()->playing; }, "zero-delay playback did not stop");
    std::cout << "PASS mouse countdown: no early output, expiry, cancellation, stale generation, zero delay, warning gate\n";
}

void LibraryParityTests(const std::filesystem::path& directory) {
    using A = shell::ShellEngine::Action;
    using Sort = shell::FileSort;
    const auto folder = directory / L"parity-library";
    std::filesystem::create_directories(folder);
    const auto alpha = folder / L"Alpha.mid", beta = folder / L"beta.mid", gamma = folder / L"Gamma.mid";
    WriteTrackFixture(alpha); WriteHeldNoteFixture(beta, 60); WriteTrackFixture(gamma);
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(alpha, now - 3h);
    std::filesystem::last_write_time(beta, now - 1h);
    std::filesystem::last_write_time(gamma, now - 2h);
    const auto config = directory / L"library-parity.json";
    nlohmann::json settings;
    { std::ifstream file(directory / L"config.json"); file >> settings; }
    settings["LEGIT_MODE_SETTINGS"]["NOTE_SKIP_CHANCE"] = 1.0;
    settings["LEGIT_MODE_SETTINGS"]["EXTRA_DELAY_CHANCE"] = 0.0;
    settings["LEGIT_MODE_SETTINGS"]["TIMING_VARIATION"] = 0.0;
    { std::ofstream file(config); file << settings; }
    {
        shell::ShellEngine engine(config);
        engine.Send({A::Scan, folder});
        Await([&] { return engine.Snapshot()->files->size() == 3; }, "library fixture did not scan");
        const auto sort = [&](Sort by, bool descending, const std::filesystem::path& first, const std::filesystem::path& last) {
            engine.Send({A::SortFiles, {}, 0, 0, descending, static_cast<double>(by)});
            Await([&] { const auto s = engine.Snapshot(); return s->fileSort == by && s->descendingFiles == descending &&
                s->files->front().path == first && s->files->back().path == last; }, "library sort produced the wrong endpoints");
        };
        sort(Sort::Name, false, alpha, gamma); sort(Sort::Name, true, gamma, alpha);
        sort(Sort::Size, false, beta, gamma); sort(Sort::Size, true, gamma, beta);
        sort(Sort::Modified, false, alpha, beta); sort(Sort::Modified, true, beta, alpha);
        Require(engine.Snapshot()->files->front().modified == std::filesystem::last_write_time(beta), "scan omitted modification dates");
        engine.Send({A::Load, beta});
        Await([&] { return engine.Snapshot()->loaded == beta; }, "library selection did not load");
        TakeCaptured();
        const auto step = [&](A action, const std::filesystem::path& expected) {
            const auto generation = engine.Snapshot()->generation;
            engine.Send({action, {}, generation});
            Await([&] { return engine.Snapshot()->generation > generation && engine.Snapshot()->loaded == expected; }, "Prev/Next lost the sorted order or wrap");
        };
        step(A::Previous, alpha); step(A::Next, beta); step(A::Next, gamma);
        Require(!engine.Snapshot()->playing && OnlyModifierReleases(TakeCaptured()), "stopped file navigation typed a note");
        engine.Send({A::LegitMode, {}, 0, 0, true});
        Await([&] { return engine.Snapshot()->legitMode; }, "Legit Mode did not enable");
        step(A::Next, alpha);
        Require(engine.Snapshot()->legitMode, "Load discarded Legit Mode");
        TakeCaptured(); engine.Send({A::Play, {}, engine.Snapshot()->generation});
        Await([&] { return engine.Snapshot()->playing; }, "Legit Mode playback did not start");
        Await([&] { return !engine.Snapshot()->playing; }, "Legit Mode playback did not finish");
        const auto skipped = TakeCaptured();
        Require(std::none_of(skipped.begin(), skipped.end(), IsNotePress), "Load reset the real Legit Mode flag and typed notes that should be skipped");
        engine.Send({A::LegitMode, {}, 0, 0, false});
        engine.Send({A::Shuffle, {}, 0, 0, true});
        Await([&] { return engine.Snapshot()->shuffle; }, "shuffle did not enable");
        const auto generation = engine.Snapshot()->generation;
        TakeCaptured(); engine.Send({A::Play, {}, generation});
        Await([&] { return engine.Snapshot()->generation > generation; }, "shuffle never advanced after playback ended");
        Require(engine.Snapshot()->loaded != alpha, "shuffle immediately repeated the same file despite alternatives");
        Await([&] { return engine.Snapshot()->playing; }, "shuffle loaded but did not play the next file");
        engine.Send({A::Stop});
        Await([&] { return !engine.Snapshot()->playing; }, "shuffle did not stop");
        const auto stoppedGeneration = engine.Snapshot()->generation;
        // Model a completion that had already queued its advance when Stop
        // arrived. Stop must invalidate that work even with the same score id.
        engine.Send({A::Next, {}, stoppedGeneration, 0, false, 1});
        TakeCaptured(); std::this_thread::sleep_for(650ms);
        Require(!engine.Snapshot()->playing && engine.Snapshot()->generation == stoppedGeneration && TakeCaptured().empty(),
                "Stop allowed shuffle to start another song");
        engine.Send({A::LegitMode, {}, 0, 0, true});
        engine.Send({A::PlaybackDelay, {}, 0, 0, false, 4});
        Await([&] { return engine.Snapshot()->playbackDelay == 4; }, "saved settings did not apply");
        engine.Send({A::SeekStep, {}, 0, 0, false, 900});
        Await([&] { return engine.Snapshot()->seekStep == 60; }, "seek step did not clamp to its maximum");
        engine.Send({A::SeekStep, {}, 0, 0, false, 0});
        Await([&] { return engine.Snapshot()->seekStep == 1; }, "seek step did not clamp to its minimum");
        engine.Send({A::SeekStep, {}, 0, 0, false, 25});
        Await([&] { return engine.Snapshot()->seekStep == 25; }, "seek step did not apply");
    }
    {
        shell::ShellEngine engine(config);
        Await([&] { return !engine.Snapshot()->curves.empty(); }, "library settings did not restart");
        const auto state = engine.Snapshot();
        Require(state->legitMode && state->shuffle && state->fileSort == Sort::Modified && state->descendingFiles &&
                state->playbackDelay == 4 && state->seekStep == 25, "parity settings failed to persist across restart");
        engine.Send({A::Load, beta});
        Await([&] { return engine.Snapshot()->loaded == beta; }, "persisted Legit Mode load did not complete");
        Require(engine.Snapshot()->legitMode, "restarted file load discarded Legit Mode");
    }
    std::cout << "PASS library sort, metadata, Prev/Next wrap, stopped navigation, shuffle completion/Stop, parity persistence\n";
}

void ConnectAndWarningTests(const std::filesystem::path& directory) {
    using A = shell::ShellEngine::Action;
    FakeMidiInput* input = nullptr;
    bool allowOpen = true;
    std::vector<DWORD> factoryThreads;
    SetMidiInputFactory([&](MidiBackend) {
        auto fake = std::make_unique<FakeMidiInput>(); fake->allowOpen = allowOpen; input = fake.get();
        factoryThreads.push_back(GetCurrentThreadId()); return fake;
    });
    struct Restore { ~Restore() { SetMidiInputFactory({}); } } restore;
    const auto caller = GetCurrentThreadId();
    const auto config = directory / L"connect-parity.json";
    std::filesystem::copy_file(directory / L"config.json", config, std::filesystem::copy_options::overwrite_existing);
    {
        shell::ShellEngine engine(config, {}, true, [] { return std::make_unique<shell::NativeConnectInput>(); });
        Await([&] { return !engine.Snapshot()->curves.empty(); }, "warning test did not initialize");
        for (auto action : {A::PlayCountdown, A::LiveOpen, A::LiveActive, A::MidiConnect, A::AutoVolumeCalibrate}) {
            engine.Send({A::ClearLog});
            Await([&] { return engine.Snapshot()->error.empty(); }, "warning error did not reset");
            TakeCaptured();
            shell::ShellEngine::Command command{action}; command.value = true; command.device = L"winmm:0|Test piano";
            engine.Send(command);
            Await([&] { return !engine.Snapshot()->error.empty(); }, "typing warning failed to reject an output route");
            Require(OnlyModifierReleases(TakeCaptured()) && factoryThreads.empty(), "unacknowledged output opened an input or typed a key");
        }
        engine.Send({A::AcknowledgeTyping});
        Await([&] { return engine.Snapshot()->typingAcknowledged; }, "warning did not acknowledge");
        shell::ShellEngine::Command open{A::LiveOpen}; open.device = L"winmm:0|Test piano";
        engine.Send(open);
        Await([&] { return engine.Snapshot()->liveActive; }, "acknowledged Midi2Key did not open");
        TakeCaptured(); input->Deliver({0x90, 60, 80});
        const auto held = TakeCaptured();
        const auto key = std::find_if(held.begin(), held.end(), IsNotePress);
        Require(key != held.end(), "Midi2Key did not attack a held note before route change");
        engine.Send({A::MidiConnect, {}, 0, 0, true});
        Await([&] { return engine.Snapshot()->midiConnect; }, "MidiConnect did not activate");
        Require(!engine.Snapshot()->liveActive, "both input routes remained active");
        Require(HasKey(TakeCaptured(), key->input.ki.wScan, false), "route change left the Midi2Key note held");
        input->Deliver({0x90, 60, 80});
        const auto protocol = TakeCaptured();
        Require(protocol.size() == 10 && protocol.front().input.ki.wScan == 0x37,
                "MidiConnect toggle did not route the real ten-event numpad protocol");
        open.device = L"winmm:1|Second piano"; engine.Send(open);
        Await([&] { return engine.Snapshot()->liveDevice == open.device; }, "MidiConnect device selection did not change");
        Require(engine.Snapshot()->midiConnect && !engine.Snapshot()->liveActive && input->openedDeviceId() == open.device,
                "changing devices lost the MidiConnect route or opened the wrong id");
        TakeCaptured(); input->Deliver({0x80, 60, 0});
        Require(TakeCaptured().size() == 10, "new device did not deliver the MidiConnect release protocol");
        engine.Send({A::Stop});
        Await([&] { return !engine.Snapshot()->midiConnect; }, "Stop did not close MidiConnect");
        const auto stopped = TakeCaptured();
        Require(!stopped.empty(), "Stop omitted the MidiConnect release sweep");
        for (const auto& event : stopped) Require(event.thread != caller, "MidiConnect release ran on the caller thread");
        allowOpen = false;
        engine.Send({A::MidiConnect, {}, 0, 0, true});
        Await([&] { return !engine.Snapshot()->error.empty(); }, "failed MidiConnect open was not reported");
        Require(!engine.Snapshot()->midiConnect && !engine.Snapshot()->liveActive, "failed route open left an active snapshot");
        allowOpen = true;
        engine.Send({A::MidiConnect, {}, 0, 0, true});
        Await([&] { return engine.Snapshot()->midiConnect; }, "MidiConnect did not recover after open failure");
        engine.Send({A::LiveActive, {}, 0, 0, true});
        Await([&] { return engine.Snapshot()->liveActive; }, "switching back to Midi2Key did not reopen input");
        Require(!engine.Snapshot()->midiConnect, "switching back left MidiConnect active");
        engine.Send({A::MidiConnect, {}, 0, 0, true});
        Await([&] { return engine.Snapshot()->midiConnect; }, "shutdown route did not activate");
        TakeCaptured();
    }
    Require(!TakeCaptured().empty(), "shutdown did not release MidiConnect");
    for (const auto thread : factoryThreads) Require(thread != caller, "an input was opened on the UI thread");
    std::cout << "PASS warning gates, real MidiConnect protocol, route/device changes, failed open, Stop/shutdown and worker ownership\n";
}

int wmain(int argc, wchar_t** argv) {
    // Before anything constructs a player, not partway through the run.
    //
    // InjectInput defaults to the real SendInput, and four tests used to
    // install this hook a few lines into themselves. Everything before the
    // first of those injected into whatever window had focus: MappingPersistence
    // and Controller both build a ShellEngine, and Action::Load and the engine
    // teardown both call stopPlayback(), which releases keys. So the suite typed
    // into the desktop, and CONTINUE-HERE.md said it did not.
    //
    // Made a process-wide invariant instead, so no test can leak by being added
    // in the wrong order or by forgetting the line. LatencyTests is a separate
    // executable and still exercises the real path on purpose.
    InjectInput = Capture;
    try {
        const auto directory = std::filesystem::current_path();
        const auto fixture = directory / L"tracks-\u97f3\u4e50.mid";
        WriteTrackFixture(fixture);
        // Used by the tracked mutation runner. The process-wide injection hook
        // above stays installed before any filtered test constructs a player.
        if (argc == 2) {
            const std::wstring group = argv[1];
            if (group == L"grouping") DeviceGroupingTests();
            else if (group == L"log") ShellLogTests(directory / L"config.json");
            else if (group == L"out-range") OutRangeSwitchTests(directory);
            else if (group == L"countdown") CountdownTests(directory);
            else if (group == L"library") LibraryParityTests(directory);
            else if (group == L"connect") ConnectAndWarningTests(directory);
            else if (group == L"curve") VelocityCurveDrawingTests(directory / L"config.json");
            else if (group == L"midi-out") MidiOutputTests(directory / L"config.json");
            else throw std::runtime_error("Unknown shell test group");
            return 0;
        }
        VelocityTelemetryTests();
        WootingMapTests();
        WootingSettingsTests();
        WootingPollTests();
        MidiStreamSplitTests();
        KsEventWalkTests();
        KernelStreamingIdentityTests();
        PortResolutionTests();
        SheetExportTests();
        TwoDeviceTests();
        ModelTests(fixture);
        MappingPersistenceTests(directory / L"config.json");
        VelocityCurveDrawingTests(directory / L"config.json");
        MidiOutputTests(directory / L"config.json");
        VelocityBatchTests(directory / L"config.json");
        ReleaseAllKeysTests(directory / L"config.json");
        ReleaseTests(directory / L"config.json");
        FolderScanTests(directory / L"config.json");
        ControllerTests(directory / L"config.json", fixture);
        LayoutTests(directory);
        AutoVolumeTests(directory / L"config.json", fixture);
        DeviceGroupingTests();
        ShellLogTests(directory / L"config.json");
        OutRangeSwitchTests(directory);
        CountdownTests(directory);
        LibraryParityTests(directory);
        ConnectAndWarningTests(directory);
        std::cout << "PASS all shell tests (injection captured in process)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
