#include "ShellEngine.hpp"
#include "PlaybackSystem.hpp"

// The engine's legacy host hooks. The shell owns its own UI and commands.
VirtualPianoPlayer* g_player = nullptr;
int g_sustainCutoff = 64;
void ShowSplashScreen(HINSTANCE) {}
void CloseSplashScreen() {}

namespace shell {
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
    auto startedAt = std::chrono::steady_clock::now();
    const auto stopPlayback = [&] {
        if (!player || !player->playback_thread) return;
        player->should_stop.store(true, std::memory_order_release);
        SetEvent(player->command_event);
        player->playback_cv.notify_all();
        if (player->playback_thread->joinable()) player->playback_thread->join();
        player->playback_thread.reset();
        player->paused.store(true, std::memory_order_release);
        player->release_all_keys();
        state.playing = false;
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
                    command.action != Action::Stop && command.action != Action::Velocity && command.action != Action::Sustain;
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
                    if (!player) player = std::make_unique<VirtualPianoPlayer>(false, config_);
                    stopPlayback();
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
                case Action::Play:
                    if (player && !state.loaded.empty() && !state.rows.empty()) {
                        stopPlayback();
                        player->restart_song();
                        state.playing = true;
                        state.position = 0;
                        startedAt = std::chrono::steady_clock::now() + 50ms;
                    }
                    break;
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
                state.position = std::clamp(std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count(), 0.0, state.duration);
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
