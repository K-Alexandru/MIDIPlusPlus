#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Fonts.hpp"
#include "ShellEngine.hpp"
#include "InputLatency.hpp"
#include <windows.h>

namespace shell {
struct Preferences {
    int skin = 0;
    bool autoSolo = true;
    bool keyMappingOpen = true;
    bool alwaysOnTop = false;
    std::filesystem::path folder;
};
class Panels {
public:
    Preferences preferences;
    bool stopHotkeyAvailable = false;
    bool velocityExpanded = false;
    bool miniMode = false;
    bool miniAutoplay = false;
    ~Panels();
    ImVec2 DesiredSize() const;
    void LoadPreferences(const std::filesystem::path& path);
    void SavePreferences(const std::filesystem::path& path) const;
    void Draw(HWND hwnd, const Fonts& fonts, const skin::Skin& design,
              float dpi, ShellEngine& engine);
private:
    char search_[256]{};
    bool descendingNames_ = false;
    std::shared_ptr<const std::vector<MidiEntry>> filteredFiles_;
    std::string filteredQuery_;
    std::vector<size_t> fileFilter_;
    void DrawKeyMapping(const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine);
    int rangeStart_ = 36;
    int selectedNote_ = -1;
    bool fullKeyboard_ = false;
    bool mappingArmed_ = false;
    float mappingDpi_ = 0;
    float seekPosition_ = 0;
    bool seeking_ = false;
    uint64_t seekGeneration_ = 0;
    uint64_t handledSheetRevision_ = 0;
    uint64_t sheetStatusGeneration_ = 0;
    std::string sheetStatus_;
    bool sheetPending_ = false;
    void DrawVelocity(const Fonts&, const skin::Skin&, float, ShellEngine&, ImVec2, ImVec2);
    void DrawSettings(const Fonts&, const skin::Skin&, float, ShellEngine&);
    void SettingsControl(const Fonts&, const skin::Skin&, float, ShellEngine&, ImVec2, float);
    void DrawMini(HWND, const Fonts&, const skin::Skin&, float, ShellEngine&, ImVec2, ImVec2);
    void DrawStatus(const Fonts&, const skin::Skin&, float, const EngineSnapshot&, ImVec2, float, float);
    bool advancedCurve_ = false;
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
    int editingStep_ = -1;
    float stepValue_ = 0;
    bool cutoffEditing_ = false;
    float cutoffPreview_ = 64;
    std::array<float, 3> wootingPreview_{0.5f, 12.f, 5.f};
    std::array<bool, 3> wootingEditing_{};
    std::array<bool, 3> wootingPending_{};
    bool scannedLive_ = false;
    bool measuring_ = false;
    int timingSource_ = 0;
    input_latency::Collector timing_;
    input_latency::Summary timingSummary_;
    double nextTimingPoll_ = 0;
};
}
