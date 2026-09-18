#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Fonts.hpp"
#include "ShellEngine.hpp"
#include "InputLatency.hpp"
#include <windows.h>

namespace shell {
// The Open buttons run a modal Win32 dialog, which no test can drive. Routing
// them through a hook lets a render test click the button and see whether the
// click arrived -- the part that was broken -- without a dialog on screen.
extern std::function<std::filesystem::path(HWND)> PickMidiFile;

struct Preferences {
    int skin = 0;
    bool autoSolo = false;
    // Never restored from the settings file: the window starts closed every run.
    bool keyMappingOpen = false;
    bool alwaysOnTop = false;
    int opacity = 100;
    std::filesystem::path folder;
};
class Panels {
public:
    Preferences preferences;
    bool stopHotkeyAvailable = false;
    // Keycap text per hotkey, empty when unbound, and whether the key
    // registered; the shell sets both each time it registers.
    std::array<std::string, kHotkeys> transportKeys{"F1", "F2", "F3", "F4", "", ""};
    std::array<bool, kHotkeys> transportKeysAvailable{};
    // The hotkey Settings is reading a key for, or -1. The shell owns the
    // read: it unregisters the hotkeys, polls, and sends the rebind.
    int hotkeyCapture = -1;
    bool velocityExpanded = false;
    bool miniMode = false;
    bool miniAutoplay = false;
    bool autoVolumeOpen = false;
    bool logOpen = false;
    // A one-frame request to open the Convert audio popover, consumed by the
    // next Draw. The menu that normally opens it needs a click a render test
    // cannot place reliably, so the test asks here instead.
    bool openConvert = false;
    // The same for the library save's confirmation: set by its menu item, or
    // by a render test, and consumed by the next Draw.
    bool openLibrarySave = false;
    // Scrolls the open Settings popover to the drum and auto-transpose
    // switches, which sit below the fold; a render scenario captures them.
    bool revealSettingsSwitches = false;
    // The same for the Hotkeys section, further down still.
    bool revealSettingsHotkeys = false;
    ~Panels();
    ImVec2 DesiredSize() const;
    // The smallest full window: Files at its 240 and the right column at its
    // 600. Mini has one size, DesiredSize.
    static ImVec2 MinimumSize() { return ImVec2(884, 560); }
    void LoadPreferences(const std::filesystem::path& path);
    void SavePreferences(const std::filesystem::path& path) const;
    void Draw(HWND hwnd, const Fonts& fonts, const skin::Skin& design,
              float dpi, ShellEngine& engine);
    // Something on screen moves by the clock alone, with no input and no new
    // snapshot behind it: the timing readout polls every 200 ms. The shell
    // draws on demand and asks this before it decides there is nothing to draw.
    bool Animating() const { return measuring_ || hotkeyCapture >= 0; }
private:
    char search_[256]{};
    FileSort fileSort_ = FileSort::Name;
    bool descendingFiles_ = false;
    std::shared_ptr<const std::vector<MidiEntry>> filteredFiles_;
    std::string filteredQuery_;
    std::vector<size_t> fileFilter_;
    void DrawKeyMapping(const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine);
    int rangeStart_ = 36;
    int selectedNote_ = -1;
    bool fullKeyboard_ = false;
    bool mappingArmed_ = false;
    bool mappingLayout88_ = true;
    float mappingDpi_ = 0;
    float seekPosition_ = 0;
    bool seeking_ = false;
    uint64_t seekGeneration_ = 0;
    uint64_t handledSheetRevision_ = 0;
    uint64_t sheetStatusGeneration_ = 0;
    std::string sheetStatus_;
    std::string sheetNote_;
    bool sheetPending_ = false;
    char convertLink_[1024]{};
    bool convertPlaylist_ = false;
    void DrawVelocity(const Fonts&, const skin::Skin&, float, ShellEngine&, ImVec2, ImVec2);
    void DrawSettings(const Fonts&, const skin::Skin&, float, ShellEngine&);
    void DrawConvert(HWND, const Fonts&, const skin::Skin&, float, ShellEngine&);
    void DrawAutoVolume(const Fonts&, const skin::Skin&, float, ShellEngine&);
    void DrawLog(HWND, const Fonts&, const skin::Skin&, float, ShellEngine&);
    // The hotkey legend: a keycap per key, its action after it. Measures
    // when draw is null. Callers push the meta face first.
    float DrawTransportHints(ImDrawList* draw, const skin::Skin& s, float dpi, ImVec2 origin) const;
    bool volumeWasOpen_ = false;
    GameWindow volumeWindow_;
    void SettingsControl(const Fonts&, const skin::Skin&, float, ShellEngine&, ImVec2, float);
    void DrawMini(HWND, const Fonts&, const skin::Skin&, float, ShellEngine&, ImVec2, ImVec2);
    void DrawStatus(const Fonts&, const skin::Skin&, float, const EngineSnapshot&, ImVec2, float, float);
    int nameOperation_ = 0;
    char curveName_[128]{};
    bool focusCurveName_ = false;
    uint64_t nameRevision_ = 0;
    uint64_t editorRevision_ = UINT64_MAX;
    uint64_t listRevision_ = UINT64_MAX;
    uint64_t histogramRevision_ = UINT64_MAX;
    std::array<float, velocity_telemetry::kBuckets> histogramHeights_{};
    bool histogramVisible_ = false;
    VelocityEdit editor_;
    std::string editorError_;
    int curveTool_ = 0;
    int activeAnchor_ = -1;
    // The anchors as the drag found them, and the dragged one's place there.
    std::vector<VelocityPoint> dragAnchors_;
    int dragIndex_ = -1;
    bool curveGesture_ = false;
    VelocityEdit curveGestureBase_;
    std::vector<VelocityPoint> freeDraw_;
    bool cutoffEditing_ = false;
    float cutoffPreview_ = 64;
    std::array<float, 3> wootingPreview_{0.5f, 12.f, 5.f};
    std::array<bool, 3> wootingEditing_{};
    std::array<bool, 3> wootingPending_{};
    bool scannedLive_ = false;
    bool scannedOutput_ = false;
    bool measuring_ = false;
    int timingSource_ = 0;
    input_latency::Collector timing_;
    input_latency::Summary timingSummary_;
    double nextTimingPoll_ = 0;
};
}
