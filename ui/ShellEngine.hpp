#pragma once
#include "TrackModel.hpp"
#include "VelocityModel.hpp"
#include "AutoVolume.hpp"
#include "LibraryModel.hpp"
#include "ShellLog.hpp"
#include "ConnectInput.hpp"
#include "DeviceModel.hpp"
#include "HotkeyNames.hpp"
#include "../MIDI++/VelocityTelemetry.hpp"
#include "../MIDI++/SheetExport.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <map>

namespace shell {

// Legit mode's amounts, in the order the snapshot, Settings and config.json
// hold them.
inline constexpr std::array<const char*, 5> kLegitAmounts{"Timing", "Tempo", "Dynamics", "Note Length", "Mistakes"};
inline constexpr std::array<const char*, 5> kLegitAmountFields{"TIMING", "TEMPO", "DYNAMICS", "NOTE_LENGTH", "MISTAKES"};
inline constexpr std::array<const char*, 3> kLegitPlayers{"Pro", "Student", "Beginner"};

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
    // The global hotkeys by config name, in kHotkeyFields order; empty is
    // unbound. The shell registers them again when the revision moves.
    std::array<std::string, kHotkeys> hotkeys{"VK_F1", "VK_F2", "VK_F3", "VK_F4", "", ""};
    uint64_t hotkeyRevision = 0;
    bool typingAcknowledged = true;
    bool legitMode = false;
    // Legit mode plays a fresh take of the file each time (MIDI++/LegitTake.hpp).
    // The player is who we assume is playing: 0 Pro, 1 Student, 2 Beginner. The
    // amounts are kLegitAmounts, each 0..1. Difficulty below zero means the
    // estimate, which is where its slider starts; the same for the hand split.
    int legitPlayer = 0;
    std::array<double, 5> legitAmounts{.15, .15, .20, .20, 0};
    double legitDifficulty = -1;
    double legitDifficultyEstimate = 0;
    bool rememberPerSong = true;
    // 0 Both, 1 Right, 2 Left. Two tracks with notes are the hands as the file
    // gives them, and then there is no split to move.
    int hands = 0;
    int handSplit = -1;
    int handSplitEstimate = 60;
    bool handsByTrack = false;
    // 0 Auto, 1 Hold, 2 Tap. Hold plays while its key is down. Tap plays the
    // next note or chord per press, held as long as the key or, with tapHolds
    // off, for the recording's own length.
    int trigger = 0;
    bool tapHolds = true;
    bool shuffle = false;
    // Config-only in the original. Drum detection labels a kit that is not on
    // channel 10 so Solo Piano leaves it out; auto-transpose sets Transpose to
    // the file's best fit at load. Both take effect when a file loads.
    bool detectDrums = true;
    bool autoTranspose = false;
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
    bool outputMidi = false;
    std::wstring outputDevice;
    std::vector<LiveDevice> outputDevices;
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
    // Set when the sheet went to the editor page rather than the clipboard.
    std::filesystem::path sheetSaved;
    // Sheet files. Everything the app writes goes under sheetsFolder, in the
    // MIDI folder's own sub-folders, so a library sorted by artist stays
    // sorted; empty means a "<MIDI folder> sheets" folder beside the library.
    // The style is a page saved from the editor, the one place sheet settings
    // live; empty means midi-converter's defaults.
    std::filesystem::path sheetsFolder;
    std::filesystem::path sheetStylePage;
    bool sheetImage = true;
    bool sheetTextFile = true;
    bool sheetPageFile = true;
    // Set, with the sheet counts, when the sheet went to files: the folder
    // they went to. Empty for the clipboard and the editor.
    std::filesystem::path sheetFilesSaved;
    // The whole library, on its own thread, reporting through the status.
    bool sheetBatchRunning = false;
    size_t sheetBatchDone = 0;
    size_t sheetBatchTotal = 0;
    size_t sheetBatchFailed = 0;
    std::string sheetBatchStatus;
    std::vector<VelocityPreset> curves;
    VelocityEdit curve;
    VelocityEdit previousCurve;
    VelocityPreset previousPreset;
    bool comparingCurve = false;
    bool hasPreviousCurve = false;
    bool canUndoCurve = false;
    bool canRedoCurve = false;
    uint64_t curveRevision = 0;
    int sustainCutoff = 64;
    std::string velocityModifier = "alt";
    std::vector<std::string> velocityModifierConflicts;
    velocity_telemetry::Snapshot playedVelocities;
    double wootingTriggerThreshold = 0.5;
    int wootingShiftAmount = 12;
    double wootingVelocityScale = 5.0;
    // Audio to MIDI, tools/mp3-to-midi run beside the app. The status is the
    // converter's latest line; a finished .mid lands in the MIDI folder.
    bool converting = false;
    bool conversionFailed = false;
    std::string conversionStatus;
    // The sign-in window shares the converter's job, so converting is also
    // true while it is open; signingIn says which of the two it is.
    bool signingIn = false;
    bool youtubeSignedIn = false;
    std::string ActiveVelocityName() const {
        return comparingCurve ? previousPreset.name + (VelocityEdited(previousCurve) ? " (edited)" : "") : VelocityName(curves, curve);
    }
};

class ShellEngine {
public:
    enum class Action { Scan, Load, Play, Stop, Mute, Solo, SoloPiano, UnmuteAll, Velocity, Sustain,
                        Pause, TogglePlayPause, Restart, Back10, Forward10, Seek, Speed, Transpose, Remap,
                        LiveScan, LiveOpen, LiveActive, LiveChannel, OutputTarget, OutputScan, OutputOpen,
                        CopySheet,
                        // The sheet editor is midi-converter's notation as a
                        // page in the browser, where every setting lives;
                        // the engine writes it to the temp folder and the
                        // panel opens it. The app keeps no sheet settings.
                        OpenSheetEditor,
                        // The open file's sheet as files under the sheets
                        // folder: image, text and editor page as chosen.
                        SaveSheetFiles,
                        CurveSelect, CurveAdjust, CurveEdit, CurveUndo, CurveRedo, CurveCompare, CurveNew,
                        CurveDuplicate, CurveRename, SustainCutoff, VelocityModifier,
                        WootingTriggerThreshold, WootingShiftAmount, WootingVelocityScale, EightyEightKeys,
                        AutoVolumeScan, AutoVolumeCalibrate, AutoVolumeOff, AutoVolumeCancel, ClearLog,
                        PlayCountdown, PlaybackDelay, AcknowledgeTyping,
                        LegitMode, Shuffle, Previous, Next, SortFiles, MidiConnect, OutRange, SeekStep,
                        // value is the switch. Each saves its config key and
                        // reloads the open file so the track list matches.
                        DetectDrums, AutoTranspose,
                        // Sheet files: path is the folder, or the style page
                        // (empty forgets it); SheetFiles sets the output named
                        // by key (image, text, page) to value. SaveLibrarySheets
                        // writes every file in the list on its own thread,
                        // SheetBatchCancel stops it, and SheetBatchProgress is
                        // that thread reporting: track done, amount failed,
                        // key the status, value whether it has finished.
                        SheetsFolder, SheetStylePage, SheetFiles, SaveLibrarySheets, SheetBatchCancel, SheetBatchProgress,
                        // path is an audio file, or key is a link. ConvertProgress is
                        // the converter's own thread reporting back: key is the
                        // text and track an audio_to_midi::Status::Kind.
                        ConvertAudio, ConvertCancel, ConvertProgress,
                        // Opens tools/mp3-to-midi/signin.py; ConvertCancel closes it.
                        YouTubeSignIn,
                        // track is the hotkey's place in kHotkeyFields and key
                        // its new config name, empty to unbind. A key another
                        // hotkey holds moves here and leaves that one unbound.
                        Hotkey,
                        // Legit mode. LegitPlayer: track is the player, and the
                        // amounts go to that player's. LegitAmount: track is the
                        // place in kLegitAmounts, or 5 for Difficulty, where an
                        // amount below zero goes back to the estimate. Hands:
                        // track. HandSplit: amount is a MIDI note, below zero
                        // the estimate. Trigger: track. TapLength: value is
                        // whether the tap key holds the note.
                        LegitPlayer, LegitAmount, Hands, HandSplit, Trigger, TapLength, RememberPerSong };
    struct Command {
        Action action;
        std::filesystem::path path;
        uint64_t generation = 0;
        size_t track = 0;
        bool value = false;
        double amount = 0;
        std::string key;
        std::wstring device;
        std::vector<VelocityPoint> anchors;
        GameWindow window;
    };
    explicit ShellEngine(std::filesystem::path config, std::shared_ptr<AutoVolumeHost> volumeHost = {},
                         bool requireTypingAcknowledgement = false, ConnectFactory connectFactory = {});
    ~ShellEngine();
    void Send(Command command);
    std::shared_ptr<const EngineSnapshot> Snapshot() const;
    // The HWND to nudge with WM_NULL when a snapshot is published. The shell
    // draws on demand and would otherwise not know the engine had moved.
    void SetWakeWindow(void* window);
    // Whether a virtual key is down, for the Hold and Tap keys. GetAsyncKeyState
    // in the app; a table in the tests. Set before the first command.
    void SetKeyProbe(std::function<bool(int)> probe);
private:
    std::function<bool(int)> keyProbe_ = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    std::mutex keyProbeMutex_;
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
    std::atomic<void*> wakeWindow_{nullptr};
    std::jthread worker_; // Last member: every dependency is initialized before Run.
};

std::string Utf8(const std::filesystem::path& path);
// Where sheet files go when no sheets folder has been chosen.
std::filesystem::path DefaultSheetsFolder(const std::filesystem::path& midiFolder);
std::string NoteName(int note);
}
