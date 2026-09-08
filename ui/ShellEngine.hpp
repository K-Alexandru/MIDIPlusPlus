#pragma once
#include "TrackModel.hpp"
#include "VelocityModel.hpp"
#include "AutoVolume.hpp"
#include "LibraryModel.hpp"
#include "ShellLog.hpp"
#include "ConnectInput.hpp"
#include "DeviceModel.hpp"
#include "../MIDI++/VelocityTelemetry.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <map>

namespace shell {
struct EngineSnapshot {
    std::shared_ptr<const std::vector<MidiEntry>> files = std::make_shared<const std::vector<MidiEntry>>();
    std::vector<TrackRow> rows;
    std::filesystem::path folder;
    std::filesystem::path loaded;
    std::string error;
    std::shared_ptr<const std::string> log = std::make_shared<const std::string>();
    uint64_t generation = 0;
    bool busy = false;
    bool playing = false;
    int playbackCountdown = 0;
    int playbackDelay = 3;
    // Seconds the -Ns / +Ns transport buttons and the F2/F3 hotkeys move by.
    int seekStep = 10;
    bool typingAcknowledged = true;
    bool legitMode = false;
    bool shuffle = false;
    FileSort fileSort = FileSort::Name;
    bool descendingFiles = false;
    // Off, which is what VirtualPianoPlayer itself defaults to. The shell used
    // to override it to on, and velocity output is not a passive feature: every
    // changed bucket types ALT plus a character drawn from
    // "1234567890qwertyuiopasdfghjklzxc", and every one of those characters is
    // also a note in the FULL mapping. A game whose script does not consume the
    // ALT-modified keypress hears that character as a second note, so a plain
    // virtual piano plays a phantom note next to any note whose velocity moved
    // to a new bucket -- and next to as many notes of a chord as change bucket.
    // Whoever has the modified script turns it on; nobody gets it unasked.
    bool velocity = false;
    bool sustain = true;
    bool eightyEightKeys = true;
    bool outRange = false;
    bool autoVolume = false;
    bool autoVolumeNeedsCalibration = false;
    int autoVolumeCountdown = 0;
    bool autoVolumeFocusing = false;
    uint64_t autoVolumeRevision = 0;
    std::vector<GameWindow> volumeWindows;
    GameWindow volumeTarget;
    std::string volumeDownKey;
    std::string volumeUpKey;
    int volumeInitial = 100;
    double position = 0;
    double duration = 0;
    // Live MIDI input. Devices are opaque backend-specific ids, never indices:
    // see the note at the top of MIDI++/MidiInput.hpp.
    std::vector<LiveDevice> devices;
    std::wstring liveDevice;
    bool liveActive = false;
    bool midiConnect = false;
    int liveChannel = -1;  // -1 listens on every channel
    double speed = 1.0;
    int transpose = 0;
    std::map<std::string, std::string> keyMappings;
    uint64_t mappingRevision = 0;
    std::shared_ptr<const std::string> sheetText = std::make_shared<const std::string>();
    size_t sheetNotes = 0;
    size_t sheetGroups = 0;
    size_t sheetMerged = 0;
    size_t sheetUnmapped = 0;
    uint64_t sheetRevision = 0;
    bool sheetReady = false;
    std::vector<VelocityPreset> curves;
    VelocityEdit curve;
    VelocityEdit previousCurve;
    VelocityPreset previousPreset;
    bool comparingCurve = false;
    bool hasPreviousCurve = false;
    uint64_t curveRevision = 0;
    int sustainCutoff = 64;
    velocity_telemetry::Snapshot playedVelocities;
    double wootingTriggerThreshold = 0.5;
    int wootingShiftAmount = 12;
    double wootingVelocityScale = 5.0;
    std::string ActiveVelocityName() const {
        return comparingCurve ? previousPreset.name + (VelocityEdited(previousCurve) ? " (edited)" : "") : VelocityName(curves, curve);
    }
};

class ShellEngine {
public:
    enum class Action { Scan, Load, Play, Stop, Mute, Solo, SoloPiano, UnmuteAll, Velocity, Sustain,
                        Pause, TogglePlayPause, Restart, Back10, Forward10, Seek, Speed, Transpose, Remap,
                        LiveScan, LiveOpen, LiveActive, LiveChannel,
                        CopySheet,
                        CurveSelect, CurveAdjust, CurveStep, CurveCompare, CurveNew,
                        CurveDuplicate, CurveRename, SustainCutoff, CurveSteps,
                        WootingTriggerThreshold, WootingShiftAmount, WootingVelocityScale, EightyEightKeys,
                        AutoVolumeScan, AutoVolumeCalibrate, AutoVolumeOff, AutoVolumeCancel, ClearLog,
                        PlayCountdown, PlaybackDelay, AcknowledgeTyping,
                        LegitMode, Shuffle, Previous, Next, SortFiles, MidiConnect, OutRange, SeekStep };
    struct Command {
        Action action;
        std::filesystem::path path;
        uint64_t generation = 0;
        size_t track = 0;
        bool value = false;
        double amount = 0;
        std::string key;
        std::wstring device;
        std::array<float, 32> samples{};
        GameWindow window;
    };
    explicit ShellEngine(std::filesystem::path config, std::shared_ptr<AutoVolumeHost> volumeHost = {},
                         bool requireTypingAcknowledgement = false, ConnectFactory connectFactory = {});
    ~ShellEngine();
    void Send(Command command);
    std::shared_ptr<const EngineSnapshot> Snapshot() const;
private:
    void Run(std::stop_token stop);
    void Publish(const EngineSnapshot& state);
    std::filesystem::path config_;
    std::shared_ptr<AutoVolumeHost> volumeHost_;
    bool requireTypingAcknowledgement_;
    ConnectFactory connectFactory_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    mutable std::shared_ptr<const EngineSnapshot> snapshot_ = std::make_shared<const EngineSnapshot>();
    std::jthread worker_; // Last member: every dependency is initialized before Run.
};

std::string Utf8(const std::filesystem::path& path);
std::string NoteName(int note);
}
