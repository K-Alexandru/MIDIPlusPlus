#include "ShellEngine.hpp"
#include "PlaybackSystem.hpp"
#include "MIDI2Key.hpp"
#include "WootingAnalog.hpp"
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
    const auto played = velocity_telemetry::snapshot();
    std::lock_guard lock(mutex_);
    if (played.revision != snapshot_->playedVelocities.revision) {
        auto copy = std::make_shared<EngineSnapshot>(*snapshot_);
        copy->playedVelocities = played;
        snapshot_ = std::move(copy);
    }
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
    // Destroyed before the player it points at, since it is declared after it.
    std::unique_ptr<MIDI2Key> live;
    uint64_t liveMappings = 0;
    int liveTranspose = 0;
    std::vector<std::chrono::nanoseconds> scoreTimes;
    // config.json is parsed once and held here. Every reader below reads this
    // copy and every writer edits it, because reparsing and rewriting the whole
    // file per edit is most of what made changing a keybind feel slow.
    nlohmann::json configJson;
    bool configDirty = false;
    std::chrono::steady_clock::time_point configDue{};
    // Long enough that a run of remaps becomes a single write, short enough
    // that the file is current by the time anyone goes to look at it.
    constexpr auto configSettle = 400ms;
    try {
        std::ifstream stream(config_);
        configJson = nlohmann::json::parse(stream);
        state.keyMappings = configJson.at("KEY_MAPPINGS").at("FULL").get<decltype(state.keyMappings)>();
    } catch (const std::exception& error) { state.error = error.what(); }
    const auto touchConfig = [&] {
        configDirty = true;
        configDue = std::chrono::steady_clock::now() + configSettle;
    };
    // Runs on the settle deadline, before anything that reads config.json from
    // disk again, and once more on shutdown. MOVEFILE_REPLACE_EXISTING is still
    // an atomic rename, so the file is never seen half written. What was
    // dropped is WRITE_THROUGH, which waited on the physical disk while the
    // keystroke that caused it went unacknowledged.
    const auto flushConfig = [&] {
        if (!configDirty) return;
        // A config that failed to parse is held as null. Writing that back
        // would replace every saved setting with an empty file.
        if (!configJson.is_object()) throw std::runtime_error("The configuration was not loaded, so it cannot be saved.");
        auto temporary = config_; temporary += L".shell-tmp";
        { std::ofstream output(temporary); output << configJson.dump(4) << '\n'; output.flush();
          if (!output) throw std::runtime_error("Cannot save the configuration."); }
        if (!MoveFileExW(temporary.c_str(), config_.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::runtime_error("Cannot replace the saved configuration.");
        configDirty = false;
    };
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
    // Live input needs a player without a file loaded: the mappings and velocity
    // settings come from the config, not from the score.
    const auto ensurePlayer = [&] {
        if (!player) {
            // The player parses config.json itself, so a pending edit has to
            // reach the disk before it looks.
            flushConfig();
            player = std::make_unique<VirtualPianoPlayer>(false, config_);
            state.keyMappings = player->full_key_mappings;
            player->enable_velocity_keypress = state.velocity;
            player->currentSustainMode = state.sustain ? SustainMode::SPACE_DOWN : SustainMode::IG;
            player->eightyEightKeyModeActive = true;
        }
    };
    const auto applyWootingSettings = [&] {
        const auto& configured = midi::Config::getInstance().wooting;
        state.wootingTriggerThreshold = configured.TRIGGER_THRESHOLD;
        state.wootingShiftAmount = configured.SHIFT_AMOUNT;
        state.wootingVelocityScale = configured.VELOCITY_SCALE;
        SetWootingAnalogSettings({static_cast<float>(configured.TRIGGER_THRESHOLD),
                                  static_cast<float>(configured.RELEASE_FRACTION),
                                  configured.SHIFT_AMOUNT,
                                  static_cast<float>(configured.VELOCITY_SCALE)});
    };
    const auto applyCurve = [&] {
        if (!player || state.curves.empty()) return;
        auto& custom = midi::Config::getInstance().playback.customVelocityCurves;
        custom.clear();
        for (size_t i = 5; i < state.curves.size(); ++i)
            custom.push_back({state.curves[i].name, state.curves[i].thresholds});
        const auto& edit = state.comparingCurve ? state.previousCurve : state.curve;
        if (VelocityEdited(edit) || state.comparingCurve) {
            const auto& preset = state.comparingCurve ? state.previousPreset : state.curves[edit.preset];
            custom.push_back({"Shell preview", VelocityThresholds(preset, edit)});
            player->setVelocityCurveIndex(5 + custom.size() - 1);
        } else player->setVelocityCurveIndex(edit.preset);
        g_sustainCutoff = state.sustainCutoff;
    };
    // Read the real built-ins through the player's public mapping API. This
    // avoids a second set of preset constants drifting from the injector.
    try {
        ensurePlayer();
        applyWootingSettings();
        const std::string keys = "1234567890qwertyuiopasdfghjklzxc";
        for (size_t i = 0; i < 5; ++i) {
            VelocityPreset preset{player->getVelocityCurveName(static_cast<midi::VelocityCurveType>(i))};
            player->setVelocityCurveIndex(i);
            for (int input = 1; input <= 127; ++input) {
                const size_t output = keys.find(player->getVelocityKey(input));
                for (size_t bucket = output; bucket < 32; ++bucket) preset.thresholds[bucket] = input;
            }
            state.curves.push_back(std::move(preset));
        }
        for (const auto& custom : midi::Config::getInstance().playback.customVelocityCurves)
            state.curves.push_back({custom.name, custom.velocityValues});
        if (configJson.contains("SHELL_VELOCITY")) {
            const auto& saved = configJson.at("SHELL_VELOCITY");
            state.curve.preset = std::min(saved.value("preset", size_t{1}), state.curves.size() - 1);
            state.curve.sensitivity = std::clamp(saved.value("sensitivity", 0.f), -50.f, 50.f);
            state.curve.contrast = std::clamp(saved.value("contrast", 0.f), 0.f, 100.f);
            if (saved.contains("samples")) {
                state.curve.samples = saved.at("samples").get<std::array<float, 32>>();
                float last = 0;
                for (auto& value : state.curve.samples) {
                    if (!std::isfinite(value)) throw std::runtime_error("Invalid saved velocity response.");
                    value = std::clamp(value, last, 1.f); last = value;
                }
                state.curve.manual = true;
            }
            state.sustainCutoff = std::clamp(saved.value("sustainCutoff", 64), 0, 127);
        }
        applyCurve();
    } catch (const std::exception& error) {
        state.curve = {};
        if (state.curves.size() >= 5) applyCurve();
        state.error = error.what();
    }
    Publish(state);
    // A curve is committed on a slider release, not per keystroke, and the
    // documented behaviour is that a failed save reports and leaves the applied
    // response alone. So this one still writes immediately, and gives up only
    // the wait on the physical disk.
    const auto saveCurves = [&](const EngineSnapshot& next) {
        configJson["CUSTOM_VELOCITY_CURVES"] = nlohmann::json::array();
        for (size_t i = 5; i < next.curves.size(); ++i)
            configJson["CUSTOM_VELOCITY_CURVES"].push_back({{"name", next.curves[i].name}, {"values", next.curves[i].thresholds}});
        auto& saved = configJson["SHELL_VELOCITY"];
        saved = {{"preset", next.curve.preset}, {"sensitivity", next.curve.sensitivity},
                 {"contrast", next.curve.contrast}, {"sustainCutoff", next.sustainCutoff}};
        if (next.curve.manual) saved["samples"] = next.curve.samples;
        touchConfig();
        flushConfig();
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
            else if (configDirty) wake_.wait_until(lock, configDue, ready);
            else wake_.wait(lock, ready);
            if (stop.stop_requested()) break;
            // Clicking a file is one Load and a Load parses a whole score, so
            // clicking through a folder otherwise parses every file passed on
            // the way to the one wanted. Only the last of a run of the same
            // command can still matter: these four all carry an absolute
            // target, never a relative step.
            while (!commands_.empty()) {
                command = std::move(commands_.front());
                commands_.pop_front();
                const bool overtaken =
                    (command.action == Action::Load || command.action == Action::Seek ||
                     command.action == Action::Speed || command.action == Action::Transpose ||
                     command.action == Action::WootingTriggerThreshold ||
                     command.action == Action::WootingShiftAmount ||
                     command.action == Action::WootingVelocityScale) &&
                    std::any_of(commands_.begin(), commands_.end(),
                                [&](const Command& queued) { return queued.action == command.action; });
                if (overtaken) continue;
                hasCommand = true;
                break;
            }
        }
        try {
            if (hasCommand) {
                const bool scoreCommand = command.action != Action::Scan && command.action != Action::Load &&
                    command.action != Action::Stop && command.action != Action::Velocity && command.action != Action::Sustain &&
                    command.action != Action::Remap && command.action != Action::LiveScan &&
                    command.action != Action::LiveOpen && command.action != Action::LiveActive &&
                    command.action != Action::LiveChannel && command.action < Action::CurveSelect;
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
                    // Not stopped again here. The stop above already ran, and a
                    // player constructed two lines up has never played: the
                    // second call only cost another sweep of release_all_keys.
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
                    applyCurve();
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
                    // The binding is applied and published now and the file
                    // catches up when the edits settle, because a remap is one
                    // keystroke and a keystroke should not wait on a disk. Every
                    // other config field survives because this edits the parsed
                    // config in place, including the fields the live-input host
                    // owns.
                    const auto note = NoteName(static_cast<int>(command.track));
                    configJson["KEY_MAPPINGS"]["FULL"][note] = command.key;
                    touchConfig();
                    state.keyMappings[note] = command.key;
                    ++state.mappingRevision;
                    applyMappings();
                    break;
                }
                case Action::LiveScan: {
                    state.devices.clear();
                    for (const auto& device : EnumerateMidiInputs())
                        state.devices.push_back({device.id, Utf8(std::filesystem::path(device.name))});
                    break;
                }
                case Action::LiveOpen:
                    if (command.device.empty()) {
                        if (live) { live->SetActive(false); live->CloseDevice(); }
                        state.liveDevice.clear();
                        state.liveActive = false;
                        break;
                    }
                    ensurePlayer();
                    if (!live) live = std::make_unique<MIDI2Key>(player.get());
                    live->SetMidiChannel(state.liveChannel);
                    live->OpenDevice(command.device);
                    state.liveDevice = live->GetSelectedDevice();
                    if (state.liveDevice.empty()) throw std::runtime_error("Cannot open that MIDI input.");
                    live->SetActive(true);
                    state.liveActive = true;
                    liveMappings = state.mappingRevision;
                    liveTranspose = state.transpose;
                    break;
                case Action::LiveActive:
                    if (!live || state.liveDevice.empty()) break;
                    live->SetActive(command.value);
                    state.liveActive = command.value;
                    break;
                case Action::LiveChannel:
                    state.liveChannel = std::clamp(static_cast<int>(command.amount), -1, 15);
                    if (live) live->SetMidiChannel(state.liveChannel);
                    break;
                case Action::WootingTriggerThreshold:
                case Action::WootingShiftAmount:
                case Action::WootingVelocityScale: {
                    if (!std::isfinite(command.amount)) break;
                    if (!configJson.is_object())
                        throw std::runtime_error("The configuration was not loaded, so it cannot be saved.");
                    auto& configured = midi::Config::getInstance().wooting;
                    const char* field = nullptr;
                    if (command.action == Action::WootingTriggerThreshold) {
                        configured.TRIGGER_THRESHOLD = std::clamp(std::round(command.amount * 100.0) / 100.0, 0.01, 1.0);
                        field = "TRIGGER_THRESHOLD";
                        configJson["WOOTING_ANALOG"][field] = configured.TRIGGER_THRESHOLD;
                    } else if (command.action == Action::WootingShiftAmount) {
                        configured.SHIFT_AMOUNT = static_cast<int>(std::round(std::clamp(command.amount, -127.0, 127.0)));
                        field = "SHIFT_AMOUNT";
                        configJson["WOOTING_ANALOG"][field] = configured.SHIFT_AMOUNT;
                    } else {
                        configured.VELOCITY_SCALE = std::clamp(std::round(command.amount * 10.0) / 10.0, 0.1, 20.0);
                        field = "VELOCITY_SCALE";
                        configJson["WOOTING_ANALOG"][field] = configured.VELOCITY_SCALE;
                    }
                    configured.validate();
                    applyWootingSettings();
                    touchConfig();
                    // Dragging previews through the backend and lets disk I/O
                    // settle. Releasing commits the last value immediately so
                    // a save failure can be shown in Settings.
                    if (command.value) flushConfig();
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
                case Action::CurveSelect:
                case Action::CurveAdjust:
                case Action::CurveStep:
                case Action::CurveSteps:
                case Action::CurveCompare:
                case Action::CurveNew:
                case Action::CurveDuplicate:
                case Action::CurveRename:
                case Action::SustainCutoff: {
                    if (state.curves.size() < 5 || !std::isfinite(command.amount)) break;
                    auto next = state;
                    const auto remember = [&] {
                        next.previousCurve = state.comparingCurve ? state.previousCurve : state.curve;
                        next.previousPreset = state.comparingCurve ? state.previousPreset : state.curves[state.curve.preset];
                        next.hasPreviousCurve = true; next.comparingCurve = false;
                    };
                    if (command.action == Action::CurveCompare) {
                        if (!next.hasPreviousCurve) break;
                        next.comparingCurve = !next.comparingCurve;
                    } else if (command.action == Action::SustainCutoff) {
                        next.sustainCutoff = static_cast<int>(std::clamp(command.amount, 0.0, 127.0));
                    } else if (command.action == Action::CurveSelect) {
                        if (command.track >= next.curves.size()) break;
                        remember(); next.curve = {}; next.curve.preset = command.track;
                    } else if (command.action == Action::CurveAdjust) {
                        remember(); next.curve.manual = false;
                        if (command.key == "sensitivity") next.curve.sensitivity = static_cast<float>(std::clamp(command.amount, -50.0, 50.0));
                        else if (command.key == "contrast") next.curve.contrast = static_cast<float>(std::clamp(command.amount, 0.0, 100.0));
                        else break;
                    } else if (command.action == Action::CurveSteps) {
                        remember(); next.curve.manual = true;
                        float previous = 0;
                        for (int i = 0; i < 32; ++i) {
                            if (!std::isfinite(command.samples[i])) throw std::runtime_error("Invalid velocity sample.");
                            next.curve.samples[i] = std::clamp(command.samples[i], previous, 1.f);
                            previous = next.curve.samples[i];
                        }
                    } else if (command.action == Action::CurveStep) {
                        if (command.track >= 32) break;
                        remember();
                        if (!next.curve.manual) for (int i = 0; i < 32; ++i)
                            next.curve.samples[i] = VelocityShape(next.curves[next.curve.preset], next.curve, i / 31.f);
                        next.curve.manual = true;
                        next.curve.samples[command.track] = std::clamp(static_cast<float>(command.amount),
                            command.track ? next.curve.samples[command.track - 1] : 0.f,
                            command.track < 31 ? next.curve.samples[command.track + 1] : 1.f);
                    } else {
                        auto name = command.key;
                        const auto first = name.find_first_not_of(" \t\r\n"), last = name.find_last_not_of(" \t\r\n");
                        if (first == std::string::npos) throw std::runtime_error("Enter a curve name.");
                        name = name.substr(first, last - first + 1);
                        if (name.size() > 120 || name.find_first_of("\r\n\t") != std::string::npos)
                            throw std::runtime_error("Use a curve name of at most 120 bytes on one line.");
                        const bool rename = command.action == Action::CurveRename && next.curve.preset >= 5;
                        for (size_t i = 0; i < next.curves.size(); ++i)
                            if ((!rename || i != next.curve.preset) && next.curves[i].name == name)
                                throw std::runtime_error("A curve already has that name.");
                        remember();
                        const auto values = command.action == Action::CurveNew ? next.curves[1].thresholds :
                            VelocityThresholds(next.curves[next.curve.preset], next.curve);
                        size_t index = next.curve.preset;
                        if (rename) next.curves[index] = {name, values};
                        else { index = next.curves.size(); next.curves.push_back({name, values}); }
                        next.curve = {}; next.curve.preset = index;
                    }
                    // Saving cannot race dispatch, and a save failure leaves the
                    // active curve untouched. Only final slider edits are queued.
                    if (command.action != Action::CurveCompare) saveCurves(next);
                    const bool resume = state.playing;
                    const auto device = state.liveDevice;
                    const bool active = state.liveActive;
                    if (live && !device.empty()) {
                        live->SetActive(false);
                        live->CloseDevice(); // Close the port before rebuilding its lookup.
                    }
                    stopPlayback();
                    if (live && !device.empty()) {
                        // Live note ownership is internal to MIDI2Key. Release its
                        // mapped keys before replacing that object and its caches.
                        live.reset();
                    }
                    next.position = state.position; next.playing = false;
                    state = std::move(next); ++state.curveRevision;
                    applyCurve();
                    if (!device.empty()) {
                        live = std::make_unique<MIDI2Key>(player.get());
                        live->SetMidiChannel(state.liveChannel);
                        live->OpenDevice(device);
                        state.liveDevice = live->GetSelectedDevice();
                        state.liveActive = active && !state.liveDevice.empty();
                        live->SetActive(state.liveActive);
                        if (state.liveDevice.empty()) state.error = "Velocity saved; MIDI input could not reopen.";
                    }
                    if (resume && state.position < state.duration) startPlayback();
                    break;
                }
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
                // Transpose and remap rewrite full_key_mappings, and live input
                // precomputes from it on SetActive. Re-arm so it keeps playing
                // the current mapping rather than the one it opened with.
                if (live && state.liveActive &&
                    (state.mappingRevision != liveMappings || state.transpose != liveTranspose)) {
                    liveMappings = state.mappingRevision;
                    liveTranspose = state.transpose;
                    live->SetActive(true);
                }
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
        state.playedVelocities = velocity_telemetry::snapshot();
        if (configDirty && std::chrono::steady_clock::now() >= configDue) {
            try { flushConfig(); }
            catch (const std::exception& error) { state.error = error.what(); }
        }
        Publish(state);
    }
    stopPlayback();
    // Last chance to write a settling edit. The window is already going, so
    // there is nowhere left to report a failure to; the rename is atomic, so a
    // failure leaves the previous config intact rather than a damaged one.
    try { flushConfig(); } catch (const std::exception&) {}
}
}
