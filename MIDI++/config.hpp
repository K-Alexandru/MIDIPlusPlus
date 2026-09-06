#pragma once

#include <string>
#include <map>
#include <stdexcept>
#include <filesystem>
#include <optional>
#include "json.hpp"

namespace midi {

    // Forward declarations
    class ConfigException : public std::runtime_error {
    public:
        explicit ConfigException(const std::string& message) : std::runtime_error(message) {}
    };

    enum class VelocityCurveType {
        LinearCoarse = 0,
        LinearFine = 1,
        ImprovedLowVolume = 2,
        Logarithmic = 3,
        Exponential = 4,
        Custom = 5
    };

    enum class NoteHandlingMode {
        FIFO,
        LIFO,
        NoHandling
    };

    // Configuration structures
    struct VolumeSettings {
        int MIN_VOLUME = 10;
        int MAX_VOLUME = 200;
        int INITIAL_VOLUME = 100;
        int VOLUME_STEP = 10;
        int ADJUSTMENT_INTERVAL_MS = 50;

        void validate() const;
    };
    struct AutoTranspose {
        bool ENABLED = false;
        std::string TRANSPOSE_UP_KEY = "VK_UP";   // Default to Up Arrow
        std::string TRANSPOSE_DOWN_KEY = "VK_DOWN"; // Default to Down Arrow

        void validate() const;
    };

    // TSC frequency calibration at startup. This used to be 20 passes of a full
    // second each: 20 seconds of busy-waiting behind the splash screen before the
    // window appeared, to measure a constant. Each pass is timed against QPC, so
    // 100ms already resolves the ratio far better than the scheduler noise the
    // median of several passes exists to reject.
    struct AutoplayerTimingAccuracy {
        int MAX_PASSES = 5;
        double MEASURE_SEC = 0.1;

        void validate() const;
    };

    struct UISettings {
        bool alwaysOnTop = false;
    };

    struct MIDISettings {
        bool DETECT_DRUMS = true;

        void validate() const;
    };

    // Legit mode: makes autoplay sound played rather than typed. Applied at
    // dispatch, never baked into the parsed score, so it can be toggled mid-song
    // and never disturbs seeking, the position readout or the reported duration.
    struct LegitModeSettings {
        bool ENABLED = false;
        double TIMING_VARIATION = 0.1;    // 0..1, scales the attack spread
        double NOTE_SKIP_CHANCE = 0.02;   // 0..1, per note-on
        double EXTRA_DELAY_CHANCE = 0.05; // 0..1, per due batch
        double EXTRA_DELAY_MIN = 0.05;    // seconds
        double EXTRA_DELAY_MAX = 0.2;     // seconds

        // Full TIMING_VARIATION spreads a chord over this many milliseconds.
        // Measured asynchrony in human piano performance is roughly 30-50 ms
        // (Goebl/Repp; see LEGIT-MODE.md), so 1.0 sits at the top of that range
        // and the 0.1 default gives a 5 ms spread.
        static constexpr double MAX_SPREAD_MS = 50.0;

        void validate() const;
    };

    // The three controls wooting-analog-midi exposes, because a Wooting with
    // none of them can only play what one static table says. Names and defaults
    // follow that app so a user who has set it up already knows these.
    struct WootingAnalogSettings {
        // "Note Trigger Threshold": how far a key travels before it counts as
        // struck. 0 to 1.
        double TRIGGER_THRESHOLD = 0.5;
        // How far back up a key has to come before it can be struck again, as a
        // fraction of the trigger. wooting-analog-midi has no such gap and a key
        // resting on the trigger stutters; ours keeps one. 0 to 1.
        double RELEASE_FRACTION = 0.6;
        // "Shift Amount": semitones added to every key while the shift key is
        // held. This is how a keyboard whose layout only reaches the white keys
        // plays a black one, so it is not an extra: set it to 1 and shift is the
        // sharp. Upstream defaults to 12 and holds an octave instead.
        int SHIFT_AMOUNT = 12;
        // "Velocity Scale": multiplies how fast the key was travelling as it
        // crossed the trigger. Applied as rate * SCALE / 100, matching upstream
        // so a number carried over from it means the same thing here.
        double VELOCITY_SCALE = 5.0;

        void validate() const;
    };

    struct HotkeySettings {
        std::string SUSTAIN_KEY = "VK_SPACE";
        std::string VOLUME_UP_KEY = "VK_RIGHT";
        std::string VOLUME_DOWN_KEY = "VK_LEFT";
        std::string PLAY_PAUSE_KEY = "VK_F1";      // Added default for play/pause
        std::string REWIND_KEY = "VK_F2";          // Added default for rewind
        std::string SKIP_KEY = "VK_F3";            // Added default for skip
        std::string EMERGENCY_EXIT_KEY = "VK_F4"; // Added default for emergency exit
        void validate() const;
    };

    struct CustomVelocityCurve {
        std::string name;
        std::array<int, 32> velocityValues;
    };

    // Modify PlaybackSettings
    struct PlaybackSettings {
        VelocityCurveType velocityCurve = VelocityCurveType::LinearCoarse;
        NoteHandlingMode noteHandlingMode = NoteHandlingMode::LIFO;
        std::vector<CustomVelocityCurve> customVelocityCurves;
        void validate() const;
    };

    class Config {
    public:
        MIDISettings midi;
        PlaybackSettings playback;
        VolumeSettings volume;
        AutoTranspose auto_transpose;
        HotkeySettings hotkeys;
        UISettings ui;
        LegitModeSettings legit_mode;
        WootingAnalogSettings wooting;
        AutoplayerTimingAccuracy autoplayer_timing;
        std::map<std::string, std::map<std::string, std::string>> key_mappings;
        std::map<std::string, std::string> controls;
        std::vector<std::string> playlistFiles;

        static Config& getInstance();

        void loadFromFile(const std::filesystem::path& path);
        void saveToFile(const std::filesystem::path& path) const;
        void validate() const;
        void setDefaults();

        // Conversion methods made public and static
        static NoteHandlingMode stringToNoteHandlingMode(const std::string& mode);
        static std::string noteHandlingModeToString(NoteHandlingMode mode);

        // Delete copy constructor and assignment operator
        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

    private:
        Config() = default;

        void validateKeyMappings() const;
    };

    // JSON conversion functions declarations
    void to_json(nlohmann::json& j, const VolumeSettings& v);
    void from_json(const nlohmann::json& j, VolumeSettings& v);
    void to_json(nlohmann::json& j, const AutoTranspose& l);
    void from_json(const nlohmann::json& j, AutoTranspose& l);
    void to_json(nlohmann::json& j, const AutoplayerTimingAccuracy& a);
    void from_json(const nlohmann::json& j, AutoplayerTimingAccuracy& a);
    void to_json(nlohmann::json& j, const MIDISettings& m);
    void from_json(const nlohmann::json& j, MIDISettings& m);
    void to_json(nlohmann::json& j, const HotkeySettings& h);
    void from_json(const nlohmann::json& j, HotkeySettings& h);
    void to_json(nlohmann::json& j, const PlaybackSettings& p);
    void from_json(const nlohmann::json& j, PlaybackSettings& p);
    void to_json(nlohmann::json& j, const Config& c);
    void from_json(const nlohmann::json& j, Config& c);
    void to_json(nlohmann::json& j, const LegitModeSettings& l);
    void from_json(const nlohmann::json& j, LegitModeSettings& l);
    void to_json(nlohmann::json& j, const UISettings& ui);
    void from_json(const nlohmann::json& j, UISettings& ui);
    void to_json(nlohmann::json& j, const WootingAnalogSettings& w);
    void from_json(const nlohmann::json& j, WootingAnalogSettings& w);

} // namespace midi