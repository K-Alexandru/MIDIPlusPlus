#include "../ui/ShellEngine.hpp"
#include "PlaybackSystem.hpp"
#include "TrackFixture.hpp"
#include <iostream>
#include <set>

using namespace std::chrono_literals;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::mutex capturedMutex;
struct Captured { INPUT input; DWORD thread; };
std::vector<Captured> captured;
UINT __fastcall Capture(ULONG count, LPINPUT inputs, int) {
    std::lock_guard lock(capturedMutex);
    for (ULONG i = 0; i < count; ++i) captured.push_back({inputs[i], GetCurrentThreadId()});
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
}

int wmain() {
    try {
        const auto directory = std::filesystem::current_path();
        const auto fixture = directory / L"tracks-\u97f3\u4e50.mid";
        WriteTrackFixture(fixture);
        ModelTests(fixture);
        ReleaseTests(directory / L"config.json");
        ControllerTests(directory / L"config.json", fixture);
        std::cout << "PASS all shell tests (injection captured in process)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
