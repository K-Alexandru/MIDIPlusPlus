#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Fonts.hpp"
#include "ShellEngine.hpp"
#include <windows.h>

namespace shell {
struct Preferences {
    int skin = 0;
    bool autoSolo = true;
    bool keyMappingOpen = true;
    std::filesystem::path folder;
};
class Panels {
public:
    Preferences preferences;
    bool stopHotkeyAvailable = false;
    void LoadPreferences(const std::filesystem::path& path);
    void SavePreferences(const std::filesystem::path& path) const;
    void Draw(HWND hwnd, const Fonts& fonts, const skin::Skin& design,
              float dpi, ShellEngine& engine);
private:
    char search_[256]{};
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
};
}
