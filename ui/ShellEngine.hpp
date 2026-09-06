#pragma once
#include "TrackModel.hpp"
#include "VelocityModel.hpp"
#include "../MIDI++/VelocityTelemetry.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <map>

namespace shell {
struct MidiEntry { std::filesystem::path path; std::string name; uintmax_t bytes = 0; };
struct LiveDevice { std::wstring id; std::string name; };
struct EngineSnapshot {
    std::shared_ptr<const std::vector<MidiEntry>> files = std::make_shared<const std::vector<MidiEntry>>();
    std::vector<TrackRow> rows;
    std::filesystem::path folder;
    std::filesystem::path loaded;
    std::string error;
    uint64_t generation = 0;
    bool busy = false;
    bool playing = false;
    bool velocity = true;
    bool sustain = true;
    double position = 0;
    double duration = 0;
    // Live MIDI input. Devices are opaque backend-specific ids, never indices:
    // see the note at the top of MIDI++/MidiInput.hpp.
    std::vector<LiveDevice> devices;
    std::wstring liveDevice;
    bool liveActive = false;
    int liveChannel = -1;  // -1 listens on every channel
    double speed = 1.0;
    int transpose = 0;
    std::map<std::string, std::string> keyMappings;
    uint64_t mappingRevision = 0;
    std::vector<VelocityPreset> curves;
    VelocityEdit curve;
    VelocityEdit previousCurve;
    VelocityPreset previousPreset;
    bool comparingCurve = false;
    bool hasPreviousCurve = false;
    uint64_t curveRevision = 0;
    int sustainCutoff = 64;
    velocity_telemetry::Snapshot playedVelocities;
    std::string ActiveVelocityName() const {
        return comparingCurve ? previousPreset.name + (VelocityEdited(previousCurve) ? " (edited)" : "") : VelocityName(curves, curve);
    }
};

class ShellEngine {
public:
    enum class Action { Scan, Load, Play, Stop, Mute, Solo, SoloPiano, UnmuteAll, Velocity, Sustain,
                        Pause, TogglePlayPause, Restart, Back10, Forward10, Seek, Speed, Transpose, Remap,
                        LiveScan, LiveOpen, LiveActive, LiveChannel,
                        CurveSelect, CurveAdjust, CurveStep, CurveCompare, CurveNew,
                        CurveDuplicate, CurveRename, SustainCutoff, CurveSteps };
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
    };
    explicit ShellEngine(std::filesystem::path config);
    ~ShellEngine();
    void Send(Command command);
    std::shared_ptr<const EngineSnapshot> Snapshot() const;
private:
    void Run(std::stop_token stop);
    void Publish(const EngineSnapshot& state);
    std::filesystem::path config_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    mutable std::shared_ptr<const EngineSnapshot> snapshot_ = std::make_shared<const EngineSnapshot>();
    std::jthread worker_; // Last member: every dependency is initialized before Run.
};

std::string Utf8(const std::filesystem::path& path);
std::string NoteName(int note);
}
