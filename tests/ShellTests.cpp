#include "../ui/ShellEngine.hpp"
#include "PlaybackSystem.hpp"
#include "TrackFixture.hpp"
#include "../MIDI++/VelocityTelemetry.hpp"
#include "../MIDI++/WootingAnalog.hpp"
#include "../MIDI++/MidiInput.hpp"
#include "../MIDI++/SheetExport.hpp"
#include "../MIDI++/config.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <vector>
#include <set>

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
    InjectInput = Capture;
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
    InjectInput = Capture; // Captures all keystrokes; nothing reaches Windows.
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

void ControllerTests(const std::filesystem::path& config, const std::filesystem::path& fixture) {
    shell::ShellEngine engine(config);
    const DWORD uiThread = GetCurrentThreadId();
    engine.Send({shell::ShellEngine::Action::Load, fixture, 0, 0, true});
    Await([&] { const auto s = engine.Snapshot(); return !s->busy && (!s->loaded.empty() || !s->error.empty()); }, "async load timeout");
    auto state = engine.Snapshot();
    if (!state->error.empty()) throw std::runtime_error(state->error);
    Require(shell::SilentTracks(state->rows) == 3, "auto Solo Piano on load");
    const auto generation = state->generation;
    InjectInput = Capture;
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

    std::cout << "PASS MIDI to virtual piano sheet: chords, spacing, wrapping and unmapped notes\n";
}

// Three index spaces once disagreed about what "device 1" meant, which is
// harmless with one device and wrong with two. Ids fixed the disagreement but
// not the resolution: RtMidi welds the port index onto every WinMM port name,
// so the name half of an id carried the very number it existed to outlive, and
// a device that had gone away resolved to whichever port was left.
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
    std::cout << "PASS WinMM port resolution across renumbering, duplicates and absent devices\n";
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

int wmain() {
    try {
        const auto directory = std::filesystem::current_path();
        const auto fixture = directory / L"tracks-\u97f3\u4e50.mid";
        WriteTrackFixture(fixture);
        VelocityTelemetryTests();
        WootingMapTests();
        WootingSettingsTests();
        WootingPollTests();
        PortResolutionTests();
        SheetExportTests();
        TwoDeviceTests();
        ModelTests(fixture);
        MappingPersistenceTests(directory / L"config.json");
        VelocityBatchTests(directory / L"config.json");
        ReleaseTests(directory / L"config.json");
        ControllerTests(directory / L"config.json", fixture);
        std::cout << "PASS all shell tests (injection captured in process)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
