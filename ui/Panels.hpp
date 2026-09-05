#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Fonts.hpp"
#include "ShellEngine.hpp"
#include <windows.h>
#include <chrono>

namespace shell {
struct Preferences {
    int skin = 0;
    bool autoSolo = true;
    std::filesystem::path folder;
};
class Panels {
public:
    Preferences preferences;
    bool stopHotkeyAvailable = false;
    void CancelCountdown() { countdown_ = false; }
    void LoadPreferences(const std::filesystem::path& path);
    void SavePreferences(const std::filesystem::path& path) const;
    void Draw(HWND hwnd, const Fonts& fonts, const skin::Skin& design,
              float dpi, ShellEngine& engine);
private:
    char search_[256]{};
    std::shared_ptr<const std::vector<MidiEntry>> filteredFiles_;
    std::string filteredQuery_;
    std::vector<size_t> fileFilter_;
    std::chrono::steady_clock::time_point playAt_{};
    uint64_t playGeneration_ = 0;
    bool countdown_ = false;
};
}
