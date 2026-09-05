#include "Fonts.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <limits>

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

ImFont* LoadSystemFont(const wchar_t* filename, const char* name) {
    wchar_t windows[MAX_PATH]{};
    if (!GetWindowsDirectoryW(windows, MAX_PATH)) return nullptr;
    std::ifstream stream(std::filesystem::path(windows) / L"Fonts" / filename,
                         std::ios::binary | std::ios::ate);
    if (!stream) return nullptr;
    const auto length = stream.tellg();
    if (length <= 0 || length > INT_MAX) return nullptr;
    const int bytes = static_cast<int>(length);
    void* data = IM_ALLOC(bytes);
    if (!data) return nullptr;
    stream.seekg(0);
    if (!stream.read(static_cast<char*>(data), bytes)) { IM_FREE(data); return nullptr; }
    ImFontConfig config;
    strcpy_s(config.Name, name);
    // ImGui owns this copy, including the source bytes used for later DPI changes.
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, bytes, 13.f, &config);
}
}

void Fonts::Load() {
    modern_[0] = LoadResourceFont(101, "IBM Plex Sans Regular");
    modern_[1] = LoadResourceFont(102, "IBM Plex Sans Medium");
    modern_[2] = LoadResourceFont(103, "IBM Plex Sans Semibold");
    classic_[0] = LoadSystemFont(L"segoeui.ttf", "Segoe UI Regular");
    classic_[1] = classic_[2] = LoadSystemFont(L"seguisb.ttf", "Segoe UI Semibold");
    ImFont* fallback = modern_[0] ? modern_[0] : classic_[0];
    if (!fallback) fallback = ImGui::GetIO().Fonts->AddFontDefault();
    for (int i = 0; i < 3; ++i) {
        if (!modern_[i]) modern_[i] = fallback;
        if (!classic_[i]) classic_[i] = classic_[0] ? classic_[0] : fallback;
    }
    ImGui::GetIO().FontDefault = classic_[0];
}

ImFont* Fonts::Get(const skin::Skin& skin, Weight weight) const {
    return (skin.type.family == "IBM Plex Sans" ? modern_ : classic_)[static_cast<int>(weight)];
}
}
