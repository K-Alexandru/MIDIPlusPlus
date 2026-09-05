#include "ShellEngine.hpp"
#include "PlaybackSystem.hpp"
#include <fstream>
#include <cmath>
#include <intrin.h>

// The engine's legacy host hooks. The shell owns its own UI and commands.
VirtualPianoPlayer* g_player = nullptr;
int g_sustainCutoff = 64;
void ShowSplashScreen(HINSTANCE) {}
void CloseSplashScreen() {}

namespace shell {
std::string NoteName(int note) {
    static constexpr const char* names[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(names[note % 12]) + std::to_string(note / 12 - 1);
}
std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

ShellEngine::ShellEngine(std::filesystem::path config)
    : config_(std::move(config)), worker_([this](std::stop_token stop) { Run(stop); }) {}

ShellEngine::~ShellEngine() {
    worker_.request_stop();
    wake_.notify_all();
    worker_.join(); // Key release and player destruction also happen on the worker.
}

void ShellEngine::Send(Command command) {
    { std::lock_guard lock(mutex_); commands_.push_back(std::move(command)); }
    wake_.notify_one();
}

std::shared_ptr<const EngineSnapshot> ShellEngine::Snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

void ShellEngine::Publish(const EngineSnapshot& state) {
    auto copy = std::make_shared<const EngineSnapshot>(state);
    std::lock_guard lock(mutex_);
    snapshot_ = std::move(copy);
}

void ShellEngine::Run(std::stop_token stop) {
    using namespace std::chrono_literals;
    EngineSnapshot state;
    std::unique_ptr<VirtualPianoPlayer> player;
    std::vector<std::chrono::nanoseconds> scoreTimes;
    try {
        std::ifstream stream(config_);
        const auto json = nlohmann::json::parse(stream);
        state.keyMappings = json.at("KEY_MAPPINGS").at("FULL").get<decltype(state.keyMappings)>();
    } catch (const std::exception& error) { state.error = error.what(); }
    Publish(state);
    const auto stopPlayback = [&] {
        if (!player) return;
        player->should_stop.store(true, std::memory_order_release);
        SetEvent(player->command_event);
        player->playback_cv.notify_all();
        if (player->playback_thread && player->playback_thread->joinable()) player->playback_thread->join();
        player->playback_thread.reset();
        if (state.playing)
            state.position = std::clamp(player->get_adjusted_time().count() / 1e9 * state.speed, 0.0, state.duration);
        player->paused.store(true, std::memory_order_release);
        player->release_all_keys();
        state.playing = false;
    };
    // Only the worker writes the clock fields. No legacy seek/speed calls run
    // concurrently with dispatch. Joining also drains the engine's batch future.
    const auto startPlayback = [&] {
        if (!player || state.loaded.empty() || state.rows.empty() || state.duration <= 0) return;
        // The inherited scheduler waits in wall nanoseconds. Scale its event
        // times here, so rates above 1x do not oversleep their next note.
        for (size_t i = 0; i < scoreTimes.size(); ++i)
            player->note_events[i].time = std::chrono::nanoseconds(static_cast<int64_t>(scoreTimes[i].count() / state.speed));
        player->current_speed = 1.0;
        player->total_adjusted_time = std::chrono::nanoseconds(static_cast<int64_t>(state.position / state.speed * 1e9));
        const auto next = std::lower_bound(player->note_events.begin(), player->note_events.end(),
            player->total_adjusted_time, [](const auto& event, auto time) { return event.time < time; });
        player->buffer_index.store(static_cast<size_t>(next - player->note_events.begin()));
        player->last_resume_tsc = __rdtsc();
        player->playback_start_time = player->last_resume_tsc;
        player->playback_started.store(true, std::memory_order_release);
        player->should_stop.store(false, std::memory_order_release);
        player->paused.store(true, std::memory_order_release);
        ResetEvent(player->command_event);
        player->toggle_play_pause();
        state.playing = true;
    };
    const auto applyMappings = [&] {
        if (!player) return;
        // Release under the old map before reaching here. Both attacks and
        // releases retain the same source-note identity after transposition.
        for (int note = 0; note < 128; ++note) {
            const int target = note + state.transpose;
            const auto found = target >= 21 && target <= 108 ? state.keyMappings.find(NoteName(target)) : state.keyMappings.end();
            player->full_key_mappings[NoteName(note)] = found == state.keyMappings.end() ? "" : found->second;
            player->pressed_keys.try_emplace(NoteName(note), false);
        }
    };
    const auto applyTracks = [&] {
        if (!player) return;
        for (const auto& row : state.rows) {
            player->set_track_mute(row.index, row.muted);
            player->set_track_solo(row.index, row.solo);
        }
    };
    while (!stop.stop_requested()) {
        Command command{Action::Stop};
        bool hasCommand = false;
        {
            std::unique_lock lock(mutex_);
            const auto ready = [&] { return stop.stop_requested() || !commands_.empty(); };
            if (state.playing) wake_.wait_for(lock, 25ms, ready);
            else wake_.wait(lock, ready);
            if (stop.stop_requested()) break;
            if (!commands_.empty()) {
                command = std::move(commands_.front());
                commands_.pop_front();
                hasCommand = true;
            }
        }
        try {
            if (hasCommand) {
                const bool scoreCommand = command.action != Action::Scan && command.action != Action::Load &&
                    command.action != Action::Stop && command.action != Action::Velocity && command.action != Action::Sustain &&
                    command.action != Action::Remap;
                if (scoreCommand && command.generation != state.generation) continue;
                state.error.clear();
                switch (command.action) {
                case Action::Scan: {
                    if (state.playing) {
                        state.error = "Stop playback before changing the MIDI folder.";
                        break;
                    }
                    state.busy = true;
                    Publish(state);
                    auto files = std::make_shared<std::vector<MidiEntry>>();
                    std::error_code error;
                    std::filesystem::directory_iterator it(command.path, error);
                    if (error) throw std::runtime_error("Cannot read MIDI folder: " + error.message());
                    for (const auto& entry : it) {
                        if (stop.stop_requested()) break;
                        if (!entry.is_regular_file(error)) continue;
                        auto extension = entry.path().extension().wstring();
                        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
                        if (extension != L".mid" && extension != L".midi") continue;
                        const auto bytes = entry.file_size(error);
                        files->push_back({entry.path(), Utf8(entry.path().filename()), error ? 0 : bytes});
                    }
                    std::sort(files->begin(), files->end(), [](const auto& a, const auto& b) { return a.name < b.name; });
                    state.files = std::move(files);
                    state.folder = command.path;
                    break;
                }
                case Action::Load: {
                    // Stop before potentially slow disk parsing, so a load cannot
                    // keep injecting while the command worker is busy.
                    stopPlayback();
                    state.busy = true;
                    Publish(state);
                    MidiParser parser;
                    auto file = parser.parse(Utf8(std::filesystem::absolute(command.path)));
                    if (file.format == 2) throw std::runtime_error("MIDI format 2 contains independent sequences. Use a format 0 or 1 file.");
                    auto rows = DescribeTracks(file);
                    if (!player) {
                        player = std::make_unique<VirtualPianoPlayer>(false, config_);
                        state.keyMappings = player->full_key_mappings;
                    }
                    stopPlayback();
                    applyMappings();
                    state.loaded.clear();
                    state.rows.clear();
                    state.duration = state.position = 0;
                    ++state.generation;
                    // Let the visible track controls own drum selection. The old
                    // parser's heuristic otherwise silently removes notes first.
                    auto& config = midi::Config::getInstance();
                    config.midi.DETECT_DRUMS = false;
                    config.auto_transpose.ENABLED = false;
                    player->legit_mode_active = false;
                    player->enable_velocity_keypress = state.velocity;
                    player->setVelocityCurveIndex(1); // Agreed default: Linear Fine.
                    player->currentSustainMode = state.sustain ? SustainMode::SPACE_DOWN : SustainMode::IG;
                    player->process_tracks(file);
                    scoreTimes.clear();
                    for (const auto& event : player->note_events) scoreTimes.push_back(event.time);
                    player->midi_file = std::move(file);
                    player->trackMuted.clear();
                    player->trackSoloed.clear();
                    for (size_t i = 0; i < player->midi_file.tracks.size(); ++i) {
                        player->trackMuted.push_back(std::make_shared<std::atomic<bool>>(false));
                        player->trackSoloed.push_back(std::make_shared<std::atomic<bool>>(false));
                    }
                    state.rows = std::move(rows);
                    if (command.value) SoloPiano(state.rows);
                    applyTracks();
                    if (!player->note_events.empty())
                        state.duration = static_cast<double>(player->note_events.back().time.count()) / 1e9;
                    player->midiFileSelected = true;
                    state.loaded = command.path;
                    break;
                }
                case Action::TogglePlayPause:
                    if (state.playing) { stopPlayback(); break; }
                    [[fallthrough]];
                case Action::Play:
                    if (!state.playing) {
                        if (state.position >= state.duration) state.position = 0;
                        startPlayback();
                    }
                    break;
                case Action::Pause: stopPlayback(); break;
                case Action::Restart:
                case Action::Seek:
                case Action::Back10:
                case Action::Forward10:
                case Action::Speed:
                case Action::Transpose:
                    if (player && std::isfinite(command.amount)) {
                        const bool resume = state.playing;
                        stopPlayback();
                        switch (command.action) {
                        case Action::Restart: state.position = 0; break;
                        case Action::Seek: state.position = command.amount; break;
                        case Action::Back10: state.position -= 10; break;
                        case Action::Forward10: state.position += 10; break;
                        case Action::Speed: state.speed = std::clamp(command.amount, .25, 2.0); break;
                        case Action::Transpose:
                            state.transpose = static_cast<int>(std::round(std::clamp(command.amount, -12.0, 12.0)));
                            applyMappings(); break;
                        default: break;
                        }
                        state.position = std::clamp(state.position, 0.0, state.duration);
                        if (resume && state.position < state.duration) startPlayback();
                    }
                    break;
                case Action::Remap: {
                    if (command.track < 21 || command.track > 108) break;
                    std::string key = command.key;
                    if (key.starts_with("ctrl+")) key.erase(0, 5);
                    if (key.size() != 1 || std::string("1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()").find(key[0]) == std::string::npos)
                        throw std::runtime_error("Use a letter, number, or shifted number for this mapping.");
                    stopPlayback();
                    // Preserve every other config field, including settings owned
                    // by the live-input host. Commit the file before publishing.
                    std::ifstream input(config_);
                    auto json = nlohmann::json::parse(input);
                    input.close();
                    const auto note = NoteName(static_cast<int>(command.track));
                    json["KEY_MAPPINGS"]["FULL"][note] = command.key;
                    auto temporary = config_; temporary += L".shell-tmp";
                    { std::ofstream output(temporary); output << json.dump(4) << '\n'; output.flush();
                      if (!output) throw std::runtime_error("Cannot save key mapping."); }
                    if (!MoveFileExW(temporary.c_str(), config_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                        throw std::runtime_error("Cannot replace the saved key mapping config.");
                    state.keyMappings[note] = command.key;
                    ++state.mappingRevision;
                    applyMappings();
                    break;
                }
                case Action::Stop:
                    stopPlayback();
                    state.position = 0;
                    break;
                case Action::Mute:
                case Action::Solo:
                    for (auto& row : state.rows) {
                        if (row.index != command.track) continue;
                        if (command.action == Action::Mute) row.muted = command.value;
                        else row.solo = command.value;
                    }
                    applyTracks();
                    break;
                case Action::SoloPiano: SoloPiano(state.rows); applyTracks(); break;
                case Action::UnmuteAll: UnmuteAll(state.rows); applyTracks(); break;
                case Action::Velocity:
                    state.velocity = command.value;
                    if (player) player->enable_velocity_keypress = command.value;
                    break;
                case Action::Sustain:
                    // Change pedal mode only while stopped; its engine state is
                    // owned by dispatch while playing.
                    if (!state.playing) {
                        state.sustain = command.value;
                        if (player) player->currentSustainMode = command.value ? SustainMode::SPACE_DOWN : SustainMode::IG;
                    }
                    break;
                }
                state.busy = false;
            }
            if (state.playing) {
                state.position = std::clamp(player->get_adjusted_time().count() / 1e9 * state.speed, 0.0, state.duration);
                if (player->playback_started.load(std::memory_order_acquire) &&
                    player->buffer_index.load(std::memory_order_acquire) >= player->note_events.size()) {
                    stopPlayback();
                    state.position = state.duration;
                }
            }
        } catch (const std::exception& error) {
            stopPlayback();
            state.error = error.what();
            state.busy = false;
        }
        Publish(state);
    }
    stopPlayback();
}
}
