#include "Fonts.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <algorithm>
#include <map>
#include <vector>

namespace shell {
namespace {
ImFont* LoadResourceFont(int id, const char* name) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return nullptr;
    HGLOBAL loaded = LoadResource(module, resource);
    void* data = LockResource(loaded);
    const DWORD size = SizeofResource(module, resource);
    if (!data || !size || size > INT_MAX) return nullptr;
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false; // The executable resource lives until exit.
    strcpy_s(config.Name, name);
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, static_cast<int>(size), 14.f, &config);
}

void MergeCjk(ImFont* destination) {
    if (!destination) return;
    // System-owned font bytes are shared by all weights. Dynamic glyph loading
    // only rasterizes characters actually used, including after a DPI change.
    static std::map<std::wstring, std::vector<char>> sources;
    wchar_t windows[MAX_PATH]{};
    if (!GetWindowsDirectoryW(windows, MAX_PATH)) return;
    for (const wchar_t* filename : {L"msyh.ttc", L"YuGothM.ttc", L"msgothic.ttc", L"malgun.ttf", L"simsun.ttc"}) {
        auto [it, inserted] = sources.try_emplace(filename);
        if (inserted) {
            std::ifstream stream(std::filesystem::path(windows) / L"Fonts" / filename, std::ios::binary | std::ios::ate);
            if (stream && stream.tellg() > 0 && stream.tellg() <= INT_MAX) {
                it->second.resize(static_cast<size_t>(stream.tellg())); stream.seekg(0);
                if (!stream.read(it->second.data(), it->second.size())) it->second.clear();
            }
        }
        auto& bytes = it->second;
        if (bytes.empty()) continue;
        ImFontConfig config;
        config.MergeMode = true;
        config.DstFont = destination;
        config.FontDataOwnedByAtlas = false;
        // Earlier font sources retain their Latin glyphs. Later sources fill
        // only missing glyphs, so the selected skin keeps its typography.
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(bytes.data(), static_cast<int>(bytes.size()), 14.f, &config);
    }
}
}

void Fonts::Load() {
    fonts_[0] = LoadResourceFont(101, "IBM Plex Sans Regular");
    fonts_[1] = LoadResourceFont(102, "IBM Plex Sans Medium");
    fonts_[2] = LoadResourceFont(103, "IBM Plex Sans Semibold");
    ImFont* fallback = fonts_[0];
    if (!fallback) fallback = ImGui::GetIO().Fonts->AddFontDefault();
    for (auto& font : fonts_) if (!font) font = fallback;
    std::vector<ImFont*> merged;
    for (ImFont* font : fonts_) {
        if (std::find(merged.begin(), merged.end(), font) != merged.end()) continue;
        MergeCjk(font); merged.push_back(font);
    }
    ImGui::GetIO().FontDefault = fonts_[0];
}

ImFont* Fonts::Get(const skin::Skin&, Weight weight) const {
    return fonts_[static_cast<int>(weight)];
}
}
