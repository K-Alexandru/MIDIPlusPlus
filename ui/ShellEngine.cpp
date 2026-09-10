#include "ShellEngine.hpp"
#include "PlaybackSystem.hpp"
#include "MIDI2Key.hpp"
#include "WootingAnalog.hpp"
#include "../MIDI++/SheetExport.hpp"
#include <fstream>
#include <cmath>
#include <intrin.h>
#include <random>

// The engine's legacy host hooks. The shell owns its own UI and commands.
VirtualPianoPlayer* g_player = nullptr;
int g_sustainCutoff = 64;
void ShowSplashScreen(HINSTANCE) {}
void CloseSplashScreen() {}

namespace shell {
namespace {
class NativeAutoVolumeHost final : public AutoVolumeHost {
    static bool Valid(const GameWindow& window) {
        const auto hwnd = reinterpret_cast<HWND>(window.id);
        DWORD process = 0;
        GetWindowThreadProcessId(hwnd, &process);
        wchar_t title[1024]{};
        GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
        return window.id && process == window.process && process != GetCurrentProcessId() &&
            IsWindow(hwnd) && IsWindowVisible(hwnd) && Utf8(std::filesystem::path(title)) == window.title;
    }
public:
    std::vector<GameWindow> Windows() override {
        std::vector<GameWindow> result;
        EnumWindows([](HWND hwnd, LPARAM context) -> BOOL {
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            if (!IsWindowVisible(hwnd) || process == GetCurrentProcessId() || GetWindow(hwnd, GW_OWNER)) return TRUE;
            wchar_t title[1024]{};
            if (GetWindowTextW(hwnd, title, static_cast<int>(std::size(title))) == 0) return TRUE;
            reinterpret_cast<std::vector<GameWindow>*>(context)->push_back(
                {reinterpret_cast<uintptr_t>(hwnd), process, Utf8(std::filesystem::path(title))});
            return TRUE;
        }, reinterpret_cast<LPARAM>(&result));
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.title < b.title; });
        return result;
    }
    bool Focus(const GameWindow& window) override {
        if (!Valid(window)) return false;
        const auto hwnd = reinterpret_cast<HWND>(window.id);
        if (IsIconic(hwnd)) ShowWindowAsync(hwnd, SW_RESTORE);
        return SetForegroundWindow(hwnd) != FALSE;
    }
    bool IsForeground(const GameWindow& window) override {
        return Valid(window) && GetForegroundWindow() == reinterpret_cast<HWND>(window.id);
    }
};
}
std::string NoteName(int note) {
    static constexpr const char* names[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(names[note % 12]) + std::to_string(note / 12 - 1);
}
std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

ShellEngine::ShellEngine(std::filesystem::path config, std::shared_ptr<AutoVolumeHost> volumeHost,
                         bool requireTypingAcknowledgement, ConnectFactory connectFactory)
    : config_(std::move(config)), volumeHost_(volumeHost ? std::move(volumeHost) : std::make_shared<NativeAutoVolumeHost>()),
      requireTypingAcknowledgement_(requireTypingAcknowledgement),
      connectFactory_(std::move(connectFactory)),
      worker_([this](std::stop_token stop) { Run(stop); }) {}

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
    const auto log = ShellLog::Instance().Snapshot();
    std::lock_guard lock(mutex_);
    if (played.revision != snapshot_->playedVelocities.revision || log != snapshot_->log) {
        auto copy = std::make_shared<EngineSnapshot>(*snapshot_);
        copy->playedVelocities = played;
        copy->log = log;
        snapshot_ = std::move(copy);
    }
    return snapshot_;
}

void ShellEngine::Publish(const EngineSnapshot& state) {
    auto copy = std::make_shared<const EngineSnapshot>(state);
    std::lock_guard lock(mutex_);
    if (!state.error.empty() && state.error != snapshot_->error)
        ShellLog::Instance().Append("[error] " + state.error + "\n");
    snapshot_ = std::move(copy);
}

void ShellEngine::Run(std::stop_token stop) {
    using namespace std::chrono_literals;
    EngineSnapshot state;
    state.typingAcknowledged = !requireTypingAcknowledgement_;
    std::chrono::steady_clock::time_point playbackDue{};
    std::mt19937 random(std::random_device{}());
    bool loadAutoSolo = false;
    bool shuffleAdvancePending = false;
    std::unique_ptr<VirtualPianoPlayer> player;
    // Destroyed before the player it points at, since it is declared after it.
    std::unique_ptr<MIDI2Key> live;
    std::unique_ptr<ConnectInput> connect;
    uint64_t liveMappings = 0;
    int liveTranspose = 0;
    std::chrono::steady_clock::time_point volumeDue{};
    bool volumePending = false;
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
        state.eightyEightKeys = configJson.value("SHELL_88_KEYS", true);
        state.outRange = configJson.value("SHELL_OUT_RANGE", false);
        state.playbackDelay = std::clamp(configJson.value("SHELL_PLAYBACK_DELAY", 3), 0, 10);
        state.seekStep = std::clamp(configJson.value("SHELL_SEEK_STEP", 10), 1, 60);
        state.shuffle = configJson.value("SHELL_SHUFFLE", false);
        state.fileSort = static_cast<FileSort>(std::clamp(configJson.value("SHELL_FILE_SORT", 0), 0, 2));
        state.descendingFiles = configJson.value("SHELL_FILE_DESCENDING", false);
        if (configJson.contains("LEGIT_MODE_SETTINGS"))
            state.legitMode = configJson["LEGIT_MODE_SETTINGS"].value("ENABLED", false);
        state.keyMappings = configJson.at("KEY_MAPPINGS").at(state.eightyEightKeys ? "FULL" : "LIMITED").get<decltype(state.keyMappings)>();
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
        state.playbackCountdown = 0;
        shuffleAdvancePending = false;
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
    const auto stopConnect = [&] {
        if (connect) { connect->Close(); connect.reset(); }
        state.midiConnect = false;
    };
    const auto stopLive = [&] {
        if (live) {
            live->SetActive(false);
            live->CloseDevice();
            if (state.liveActive && player) player->release_every_mapped_key();
            live.reset();
        }
        state.liveActive = false;
    };
    const auto cancelVolume = [&] {
        volumePending = false;
        state.autoVolumeCountdown = 0;
        state.autoVolumeFocusing = false;
    };
    const auto invalidateVolume = [&] {
        if (state.autoVolume || volumePending) state.autoVolumeNeedsCalibration = true;
        cancelVolume();
        state.autoVolume = false;
        if (player) player->enable_volume_adjustment.store(false, std::memory_order_release);
    };
    // Only the worker writes the clock fields. No legacy seek/speed calls run
    // concurrently with dispatch. Joining also drains the engine's batch future.
    const auto startPlayback = [&] {
        if (!state.typingAcknowledged) throw std::runtime_error("Read the typing warning in the app before starting output.");
        if (!player || state.loaded.empty() || state.rows.empty() || state.duration <= 0) return;
        stopConnect();
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
            auto& mappings = state.eightyEightKeys ? player->full_key_mappings : player->limited_key_mappings;
            mappings[NoteName(note)] = found == state.keyMappings.end() ? "" : found->second;
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
            player->enable_velocity_keypress = state.velocity;
            player->currentSustainMode = state.sustain ? SustainMode::SPACE_DOWN : SustainMode::IG;
            player->eightyEightKeyModeActive = state.eightyEightKeys;
            player->ENABLE_OUT_OF_RANGE_TRANSPOSE = state.outRange && !state.eightyEightKeys;
            player->legit_mode_active = state.legitMode;
            applyMappings();
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
        state.volumeDownKey = midi::Config::getInstance().hotkeys.VOLUME_DOWN_KEY;
        state.volumeUpKey = midi::Config::getInstance().hotkeys.VOLUME_UP_KEY;
        state.volumeInitial = midi::Config::getInstance().volume.INITIAL_VOLUME;
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
            if (saved.contains("anchors")) {
                for (const auto& point : saved.at("anchors")) {
                    if (!point.is_array() || point.size() != 2) throw std::runtime_error("Invalid saved velocity anchor.");
                    state.curve.anchors.push_back({point[0].get<float>(), point[1].get<float>()});
                }
                state.curve.anchors = VelocityLegalAnchors(std::move(state.curve.anchors));
            } else if (saved.contains("samples")) {
                // One-time migration from the retired 32-value editing model.
                const auto samples = saved.at("samples").get<std::array<float, 32>>();
                for (int i = 0; i < 32; ++i) {
                    if (!std::isfinite(samples[i])) throw std::runtime_error("Invalid saved velocity response.");
                    state.curve.anchors.push_back({i / 31.f, std::clamp(samples[i], 0.f, 1.f)});
                }
                state.curve.anchors = VelocityLegalAnchors(
                    velocity_detail::Simplify(state.curve.anchors, .004f));
            }
            state.sustainCutoff = std::clamp(saved.value("sustainCutoff", 64), 0, 127);
        }
        applyCurve();
    } catch (const std::exception& error) {
        state.curve = {};
        if (state.curves.size() >= 5) applyCurve();
        state.error = error.what();
    }
    VelocityHistory curveHistory;
    curveHistory.Reset(state.curve);
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
        if (!next.curve.anchors.empty()) {
            saved["anchors"] = nlohmann::json::array();
            for (const auto& point : next.curve.anchors) saved["anchors"].push_back({point.x, point.y});
        }
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
    const auto invalidateSheet = [&] {
        state.sheetText = std::make_shared<const std::string>();
        state.sheetNotes = state.sheetGroups = state.sheetMerged = state.sheetUnmapped = 0;
        state.sheetReady = false;
    };
    while (!stop.stop_requested()) {
        Command command{Action::Stop};
        bool hasCommand = false;
        {
            std::unique_lock lock(mutex_);
            const auto ready = [&] { return stop.stop_requested() || !commands_.empty(); };
            if (state.playing || volumePending || state.playbackCountdown) wake_.wait_for(lock, 25ms, ready);
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
                     command.action == Action::WootingVelocityScale ||
                     command.action == Action::CopySheet) &&
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
                if (!state.typingAcknowledged &&
                    (command.action == Action::LiveOpen && !command.device.empty() ||
                     command.action == Action::LiveActive && command.value ||
                     command.action == Action::MidiConnect && command.value ||
                     command.action == Action::AutoVolumeCalibrate))
                    throw std::runtime_error("Read the typing warning in the app before starting output.");
                // A transport or mapping command cancels an armed calibration
                // before it can focus another window. Reopening the dialog is
                // not permission to send keys; only Calibrate starts a sweep.
                if (volumePending && command.action != Action::AutoVolumeCalibrate &&
                    command.action != Action::AutoVolumeScan && command.action != Action::Scan)
                    cancelVolume();
                state.error.clear();
                switch (command.action) {
                case Action::MidiConnect:
                    if (!command.value) { stopConnect(); break; }
                    if (state.liveDevice.empty()) throw std::runtime_error("Choose a MIDI input before enabling MidiConnect.");
                    stopPlayback();
                    stopLive();
                    stopConnect();
                    if (!connectFactory_) throw std::runtime_error("MidiConnect is unavailable in this host.");
                    connect = connectFactory_();
                    if (!connect || !connect->Open(state.liveDevice)) {
                        stopConnect();
                        throw std::runtime_error("Cannot open that MIDI input for MidiConnect.");
                    }
                    connect->Activate(true);
                    state.midiConnect = true;
                    break;
                case Action::LegitMode:
                    state.legitMode = command.value;
                    midi::Config::getInstance().legit_mode.ENABLED = command.value;
                    if (player && player->legit_mode_active != command.value) player->toggle_legit_mode();
                    configJson["LEGIT_MODE_SETTINGS"]["ENABLED"] = command.value;
                    touchConfig();
                    break;
                case Action::Shuffle:
                    state.shuffle = command.value;
                    configJson["SHELL_SHUFFLE"] = command.value;
                    touchConfig();
                    break;
                case Action::SortFiles: {
                    if (!std::isfinite(command.amount)) break;
                    state.fileSort = static_cast<FileSort>(static_cast<int>(std::clamp(command.amount, 0.0, 2.0)));
                    state.descendingFiles = command.value;
                    auto files = std::make_shared<std::vector<MidiEntry>>(*state.files);
                    std::sort(files->begin(), files->end(), [&](const auto& a, const auto& b) {
                        return FileBefore(a, b, state.fileSort, state.descendingFiles);
                    });
                    state.files = std::move(files);
                    configJson["SHELL_FILE_SORT"] = static_cast<int>(state.fileSort);
                    configJson["SHELL_FILE_DESCENDING"] = state.descendingFiles;
                    touchConfig();
                    break;
                }
                case Action::ClearLog: ShellLog::Instance().Clear(); break;
                case Action::AcknowledgeTyping: state.typingAcknowledged = true; break;
                case Action::PlaybackDelay:
                    if (std::isfinite(command.amount)) {
                        state.playbackDelay = static_cast<int>(std::clamp(command.amount, 0.0, 10.0));
                        configJson["SHELL_PLAYBACK_DELAY"] = state.playbackDelay;
                        touchConfig();
                        state.playbackCountdown = 0;
                    }
                    break;
                case Action::SeekStep:
                    if (std::isfinite(command.amount)) {
                        state.seekStep = static_cast<int>(std::clamp(command.amount, 1.0, 60.0));
                        configJson["SHELL_SEEK_STEP"] = state.seekStep;
                        touchConfig();
                    }
                    break;
                case Action::PlayCountdown:
                    if (command.generation != state.generation) break;
                    if (state.playing || state.playbackCountdown) { stopPlayback(); break; }
                    if (!state.typingAcknowledged) throw std::runtime_error("Read the typing warning in the app before starting output.");
                    if (state.loaded.empty() || state.rows.empty()) break;
                    if (state.position >= state.duration) state.position = 0;
                    if (state.playbackDelay == 0) startPlayback();
                    else {
                        state.playbackCountdown = state.playbackDelay;
                        playbackDue = std::chrono::steady_clock::now() + std::chrono::seconds(state.playbackDelay);
                    }
                    break;
                case Action::Scan: {
                    if (state.playing) {
                        state.error = "Stop playback before changing the MIDI folder.";
                        break;
                    }
                    state.busy = true;
                    Publish(state);
                    // Sub-folders are searched too, and a file found in one is
                    // named by its path relative to the folder you chose. The
                    // original app browses instead: it lists folders as rows
                    // with a ".." to go up, so you see one directory at a time.
                    // A flat list is the better fit here because this panel has
                    // a search box, and searching your whole library beats
                    // searching whichever directory you last clicked into.
                    auto files = std::make_shared<std::vector<MidiEntry>>();
                    std::error_code error;
                    // Permission-denied folders are stepped over rather than
                    // ending the scan, and directory symlinks are not followed,
                    // which is what stops a junction pointing at its own parent
                    // from recursing forever.
                    std::filesystem::recursive_directory_iterator it(
                        command.path, std::filesystem::directory_options::skip_permission_denied, error);
                    if (error) throw std::runtime_error("Cannot read MIDI folder: " + error.message());
                    const std::filesystem::recursive_directory_iterator end;
                    // A music library is not 32 deep. The limit is here so that
                    // a pathological tree costs a bounded walk rather than the
                    // whole session.
                    constexpr int kMaxDepth = 32;
                    // And a guard for the folder picked by mistake. Pointing
                    // this at a drive root used to be harmless because the scan
                    // was one directory; now it is not. Stopping with a message
                    // beats both hanging and silently listing half a disk.
                    constexpr size_t kMaxFiles = 20000;
                    while (it != end) {
                        if (stop.stop_requested()) break;
                        const auto& entry = *it;
                        std::error_code entryError;
                        if (entry.is_regular_file(entryError) && !entryError) {
                            auto extension = entry.path().extension().wstring();
                            std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
                            if (extension == L".mid" || extension == L".midi") {
                                std::error_code sizeError, relativeError;
                                const auto bytes = entry.file_size(sizeError);
                                auto shown = std::filesystem::relative(entry.path(), command.path, relativeError);
                                if (relativeError || shown.empty()) shown = entry.path().filename();
                                std::error_code timeError;
                                const auto modified = entry.last_write_time(timeError);
                                files->push_back({entry.path(), Utf8(shown), sizeError ? 0 : bytes,
                                    timeError ? std::filesystem::file_time_type{} : modified});
                            }
                        }
                        if (files->size() >= kMaxFiles) {
                            state.error = "Stopped at " + std::to_string(kMaxFiles) +
                                          " files. Choose a folder with fewer sub-folders in it.";
                            break;
                        }
                        if (it.depth() >= kMaxDepth) it.disable_recursion_pending();
                        std::error_code step;
                        it.increment(step);
                        // An increment that fails may not have advanced, so
                        // carrying on would spin on the same entry.
                        if (step) break;
                    }
                    std::sort(files->begin(), files->end(), [&](const auto& a, const auto& b) {
                        return FileBefore(a, b, state.fileSort, state.descendingFiles);
                    });
                    state.files = std::move(files);
                    state.folder = command.path;
                    break;
                }
                case Action::Previous:
                case Action::Next: {
                    if (command.amount == 1 && (!shuffleAdvancePending || !state.shuffle)) break;
                    if (command.generation != state.generation || state.files->empty()) break;
                    const auto& files = *state.files;
                    const auto found = std::find_if(files.begin(), files.end(), [&](const auto& file) { return file.path == state.loaded; });
                    size_t index = command.action == Action::Previous ? files.size() - 1 : 0;
                    if (found != files.end()) {
                        const auto current = static_cast<size_t>(found - files.begin());
                        index = command.action == Action::Previous ? (current + files.size() - 1) % files.size() : (current + 1) % files.size();
                        if (command.amount == 1 && state.shuffle && files.size() > 1) {
                            index = std::uniform_int_distribution<size_t>(0, files.size() - 2)(random);
                            if (index >= current) ++index;
                        }
                    }
                    command.path = files[index].path;
                    command.amount = state.playing || command.amount == 1 ? 1 : 0;
                    command.value = loadAutoSolo;
                    [[fallthrough]];
                }
                case Action::Load: {
                    const bool resumeAfterLoad = command.action != Action::Load && command.amount == 1;
                    invalidateVolume();
                    // Stop before potentially slow disk parsing, so a load cannot
                    // keep injecting while the command worker is busy.
                    stopPlayback();
                    state.busy = true;
                    Publish(state);
                    MidiParser parser;
                    auto file = parser.parse(Utf8(std::filesystem::absolute(command.path)));
                    if (file.format == 2) throw std::runtime_error("MIDI format 2 contains independent sequences. Use a format 0 or 1 file.");
                    auto rows = DescribeTracks(file);
                    ensurePlayer();
                    // Not stopped again here. The stop above already ran, and a
                    // player constructed two lines up has never played: the
                    // second call only cost another sweep of release_all_keys.
                    applyMappings();
                    state.loaded.clear();
                    state.rows.clear();
                    state.duration = state.position = 0;
                    invalidateSheet();
                    ++state.generation;
                    // Let the visible track controls own drum selection. The old
                    // parser's heuristic otherwise silently removes notes first.
                    auto& config = midi::Config::getInstance();
                    config.midi.DETECT_DRUMS = false;
                    config.auto_transpose.ENABLED = false;
                    player->legit_mode_active = state.legitMode;
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
                    loadAutoSolo = command.value;
                    if (resumeAfterLoad) startPlayback();
                    break;
                }
                case Action::TogglePlayPause:
                    if (state.playbackCountdown) { stopPlayback(); break; }
                    if (state.playing) { stopPlayback(); break; }
                    [[fallthrough]];
                case Action::Play:
                    state.playbackCountdown = 0;
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
                        case Action::Back10: state.position -= state.seekStep; break;
                        case Action::Forward10: state.position += state.seekStep; break;
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
                    if (!state.eightyEightKeys && (command.track < 36 || command.track > 96))
                        throw std::runtime_error("The 61-key layout covers C2 to C7. Switch to 88 keys to map this note.");
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
                    configJson["KEY_MAPPINGS"][state.eightyEightKeys ? "FULL" : "LIMITED"][note] = command.key;
                    touchConfig();
                    state.keyMappings[note] = command.key;
                    ++state.mappingRevision;
                    applyMappings();
                    invalidateSheet();
                    break;
                }
                case Action::LiveScan: {
                    state.devices.clear();
                    for (const auto& device : EnumerateMidiInputs())
                        state.devices.push_back({device.id, Utf8(std::filesystem::path(device.group.empty() ? device.name : device.group)),
                            device.group, device.backend});
                    break;
                }
                case Action::LiveOpen: {
                    const bool connectRoute = state.midiConnect;
                    stopConnect();
                    stopLive();
                    if (command.device.empty()) {
                        if (live) { live->SetActive(false); live->CloseDevice(); }
                        state.liveDevice.clear();
                        state.liveActive = false;
                        break;
                    }
                    if (connectRoute) {
                        if (!connectFactory_) throw std::runtime_error("MidiConnect is unavailable in this host.");
                        state.liveDevice.clear();
                        connect = connectFactory_();
                        if (!connect || !connect->Open(command.device)) {
                            stopConnect();
                            throw std::runtime_error("Cannot open that MIDI input for MidiConnect.");
                        }
                        state.liveDevice = command.device;
                        connect->Activate(true);
                        state.midiConnect = true;
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
                }
                case Action::LiveActive:
                    if (command.value) stopConnect();
                    if (state.liveDevice.empty()) break;
                    if (!live && command.value) {
                        ensurePlayer();
                        live = std::make_unique<MIDI2Key>(player.get());
                        live->SetMidiChannel(state.liveChannel);
                        live->OpenDevice(state.liveDevice);
                        if (live->GetSelectedDevice().empty()) throw std::runtime_error("Cannot reopen that MIDI input.");
                    }
                    if (!live) break;
                    live->SetActive(command.value);
                    state.liveActive = command.value;
                    if (!command.value) {
                        live->CloseDevice();
                        player->release_every_mapped_key();
                        live.reset();
                    }
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
                    stopLive();
                    stopConnect();
                    break;
                case Action::Mute:
                case Action::Solo:
                    for (auto& row : state.rows) {
                        if (row.index != command.track) continue;
                        if (command.action == Action::Mute) row.muted = command.value;
                        else row.solo = command.value;
                    }
                    applyTracks();
                    invalidateSheet();
                    break;
                case Action::SoloPiano: SoloPiano(state.rows); applyTracks(); invalidateSheet(); break;
                case Action::UnmuteAll: UnmuteAll(state.rows); applyTracks(); invalidateSheet(); break;
                case Action::CopySheet: {
                    if (!player || state.loaded.empty()) break;
                    const bool anySolo = AnySolo(state.rows);
                    const auto audible = [&](int track) {
                        const auto row = std::find_if(state.rows.begin(), state.rows.end(),
                            [&](const TrackRow& candidate) { return candidate.index == static_cast<size_t>(track); });
                        return row != state.rows.end() && TrackAudible(*row, anySolo);
                    };
                    std::vector<sheet::Note> notes;
                    notes.reserve(player->note_events.size() / 2);
                    for (const auto& event : player->note_events) {
                        if (event.action != EventType::Press || event.note_or_control == "sustain" || !audible(event.trackIndex)) continue;
                        notes.push_back({static_cast<double>(event.time.count()) / 1e9, std::string(event.note_or_control)});
                    }
                    sheet::Options options;
                    if (!player->midi_file.tempoChanges.empty()) {
                        const auto tempo = std::min_element(player->midi_file.tempoChanges.begin(), player->midi_file.tempoChanges.end(),
                            [](const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; });
                        options.beatSeconds = static_cast<double>(tempo->microsecondsPerQuarter) / 1e6;
                    }
                    auto result = sheet::ToVirtualPiano(std::move(notes), state.keyMappings, options);
                    state.sheetText = std::make_shared<const std::string>(std::move(result.text));
                    state.sheetNotes = result.notes;
                    state.sheetGroups = result.groups;
                    state.sheetMerged = result.merged;
                    state.sheetUnmapped = result.unmapped;
                    state.sheetReady = true;
                    ++state.sheetRevision;
                    break;
                }
                case Action::CurveSelect:
                case Action::CurveAdjust:
                case Action::CurveEdit:
                case Action::CurveUndo:
                case Action::CurveRedo:
                case Action::CurveCompare:
                case Action::CurveNew:
                case Action::CurveDuplicate:
                case Action::CurveRename:
                case Action::SustainCutoff: {
                    if (state.curves.size() < 5 || !std::isfinite(command.amount)) break;
                    auto next = state;
                    auto nextHistory = curveHistory;
                    bool changedCurve = false;
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
                        remember(); next.curve = {}; next.curve.preset = command.track; changedCurve = true;
                    } else if (command.action == Action::CurveAdjust) {
                        remember(); next.curve.anchors.clear();
                        if (command.key == "sensitivity") next.curve.sensitivity = static_cast<float>(std::clamp(command.amount, -50.0, 50.0));
                        else if (command.key == "contrast") next.curve.contrast = static_cast<float>(std::clamp(command.amount, 0.0, 100.0));
                        else break;
                        changedCurve = true;
                    } else if (command.action == Action::CurveEdit) {
                        if (command.anchors.size() < 2 || command.anchors.size() > 256) break;
                        remember();
                        next.curve.anchors = VelocityLegalAnchors(command.anchors);
                        next.curve.sensitivity = next.curve.contrast = 0;
                        changedCurve = true;
                    } else if (command.action == Action::CurveUndo || command.action == Action::CurveRedo) {
                        remember();
                        const bool changed = command.action == Action::CurveUndo ? nextHistory.Undo() : nextHistory.Redo();
                        if (!changed) break;
                        next.curve = nextHistory.current;
                        changedCurve = true;
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
                        changedCurve = true;
                    }
                    if (changedCurve && command.action != Action::CurveUndo && command.action != Action::CurveRedo)
                        nextHistory.Commit(next.curve);
                    next.canUndoCurve = !nextHistory.undo.empty();
                    next.canRedoCurve = !nextHistory.redo.empty();
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
                    state = std::move(next); curveHistory = std::move(nextHistory); ++state.curveRevision;
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
                case Action::EightyEightKeys:
                case Action::OutRange: {
                    const bool layoutChange = command.action == Action::EightyEightKeys;
                    if ((layoutChange ? state.eightyEightKeys : state.outRange) == command.value) break;
                    const bool layout88 = layoutChange ? command.value : state.eightyEightKeys;
                    auto mappings = configJson.at("KEY_MAPPINGS").at(layout88 ? "FULL" : "LIMITED")
                        .get<decltype(state.keyMappings)>();
                    const auto device = state.liveDevice;
                    const bool active = state.liveActive;
                    // Closing joins callbacks before any lookup changes. Live
                    // note ownership is private, so release the outgoing map
                    // in full and replace its owner before building new caches.
                    if (live) { live->SetActive(false); live->CloseDevice(); }
                    stopPlayback();
                    if (live) { player->release_every_mapped_key(); live.reset(); }
                    state.liveActive = false;
                    if (layoutChange) state.eightyEightKeys = command.value;
                    else state.outRange = command.value;
                    state.keyMappings = std::move(mappings);
                    player->eightyEightKeyModeActive = state.eightyEightKeys;
                    player->ENABLE_OUT_OF_RANGE_TRANSPOSE = state.outRange && !state.eightyEightKeys;
                    applyMappings();
                    ++state.mappingRevision;
                    invalidateSheet();
                    configJson[layoutChange ? "SHELL_88_KEYS" : "SHELL_OUT_RANGE"] = command.value;
                    touchConfig();
                    if (!device.empty() && !state.midiConnect) {
                        live = std::make_unique<MIDI2Key>(player.get());
                        live->SetMidiChannel(state.liveChannel);
                        live->OpenDevice(device);
                        state.liveDevice = live->GetSelectedDevice();
                        state.liveActive = active && !state.liveDevice.empty();
                        live->SetActive(state.liveActive);
                        liveMappings = state.mappingRevision;
                        liveTranspose = state.transpose;
                        if (state.liveDevice.empty()) state.error = "Layout changed; MIDI input could not reopen.";
                    }
                    break;
                }
                case Action::AutoVolumeScan:
                    state.volumeWindows = volumeHost_->Windows();
                    break;
                case Action::AutoVolumeCalibrate: {
                    if (command.generation != state.generation) break;
                    const auto windows = volumeHost_->Windows();
                    const auto found = std::find_if(windows.begin(), windows.end(), [&](const auto& window) {
                        return window.id == command.window.id && window.process == command.window.process &&
                            window.title == command.window.title;
                    });
                    if (found == windows.end()) throw std::runtime_error("That game window changed or closed. Refresh the list and select it again.");
                    ensurePlayer();
                    const auto& volume = midi::Config::getInstance().volume;
                    if (volume.VOLUME_STEP <= 0 || volume.MIN_VOLUME < 0 || volume.MAX_VOLUME > 1000 ||
                        volume.MAX_VOLUME < volume.MIN_VOLUME || volume.INITIAL_VOLUME < volume.MIN_VOLUME ||
                        volume.INITIAL_VOLUME > volume.MAX_VOLUME)
                        throw std::runtime_error("Invalid AutoVol range or step in config.json.");
                    invalidateVolume();
                    const auto device = state.liveDevice;
                    if (live) { live->SetActive(false); live->CloseDevice(); }
                    stopPlayback();
                    if (live) { player->release_every_mapped_key(); live.reset(); }
                    state.liveActive = false;
                    if (!device.empty()) {
                        live = std::make_unique<MIDI2Key>(player.get());
                        live->SetMidiChannel(state.liveChannel);
                        live->OpenDevice(device);
                        state.liveDevice = live->GetSelectedDevice();
                        if (state.liveDevice.empty()) throw std::runtime_error("MIDI input could not reopen. Calibration was not started.");
                    }
                    state.volumeTarget = *found;
                    state.autoVolumeNeedsCalibration = true;
                    state.autoVolumeCountdown = 3;
                    volumePending = true;
                    volumeDue = std::chrono::steady_clock::now() + 3s;
                    break;
                }
                case Action::AutoVolumeOff:
                    invalidateVolume();
                    state.autoVolumeNeedsCalibration = false;
                    break;
                case Action::AutoVolumeCancel:
                    cancelVolume();
                    break;
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
            if (volumePending && !stop.stop_requested()) {
                const auto now = std::chrono::steady_clock::now();
                if (!state.autoVolumeFocusing && now >= volumeDue) {
                    state.autoVolumeCountdown = 0;
                    if (!volumeHost_->Focus(state.volumeTarget))
                        throw std::runtime_error("Could not focus the selected game. AutoVol remains off; select the game and try again.");
                    state.autoVolumeFocusing = true;
                    volumeDue = now + 500ms;
                } else if (!state.autoVolumeFocusing) {
                    state.autoVolumeCountdown = std::max(1, static_cast<int>(std::ceil(
                        std::chrono::duration<double>(volumeDue - now).count())));
                }
                if (state.autoVolumeFocusing) {
                    if (volumeHost_->IsForeground(state.volumeTarget)) {
                        // This call already calibrates. Calling calibrate_volume
                        // as well would repeat the entire key sweep.
                        player->toggle_volume_adjustment();
                        state.autoVolume = true;
                        state.autoVolumeNeedsCalibration = false;
                        ++state.autoVolumeRevision;
                        cancelVolume();
                    } else if (now >= volumeDue) {
                        throw std::runtime_error("The selected game did not keep focus. AutoVol remains off.");
                    }
                }
            }
            if (state.playbackCountdown && !stop.stop_requested()) {
                const auto remaining = std::chrono::duration<double>(playbackDue - std::chrono::steady_clock::now()).count();
                if (remaining <= 0) { state.playbackCountdown = 0; startPlayback(); }
                else state.playbackCountdown = static_cast<int>(std::ceil(remaining));
            }
            if (state.playing) {
                state.position = std::clamp(player->get_adjusted_time().count() / 1e9 * state.speed, 0.0, state.duration);
                if (player->playback_started.load(std::memory_order_acquire) &&
                    player->buffer_index.load(std::memory_order_acquire) >= player->note_events.size()) {
                    stopPlayback();
                    state.position = state.duration;
                    if (state.shuffle && !state.files->empty()) {
                        shuffleAdvancePending = true;
                        Send({Action::Next, {}, state.generation, 0, loadAutoSolo, 1});
                    }
                }
            }
        } catch (const std::exception& error) {
            if (volumePending) invalidateVolume();
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
    stopLive();
    stopConnect();
    // Last chance to write a settling edit. The window is already going, so
    // there is nowhere left to report a failure to; the rename is atomic, so a
    // failure leaves the previous config intact rather than a damaged one.
    try { flushConfig(); } catch (const std::exception&) {}
}
}
