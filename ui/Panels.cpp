#include "Panels.hpp"
#include "json.hpp"
#include <commdlg.h>
#include <shobjidl.h>
#include <fstream>
#include <cmath>

namespace shell {
namespace {
ImU32 Colour(skin::Argb c) { return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, (c >> 24) & 255); }
// Browser CSS sizes use the font em. stb_truetype uses ascent minus descent.
// Ratios from the shipped hhea/head tables: Segoe 2724/2048, Plex 1300/1000.
// Keep this correction local to the two panels measured for this port.
float SpecFontScale(const skin::Skin& s) { return s.type.family == "IBM Plex Sans" ? 1.3f : 2724.f / 2048.f; }
enum class Icon { Folder, Open, Refresh, Settings, Sun, Moon, Play, Pause, Back, Forward,
                  Minus, Plus, Left, Right, Close, Keyboard, Speaker, Muted, Solo, Piano };

void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 min, float side, ImU32 ink, float dpi) {
    const auto p = [&](float x, float y) { return ImVec2(min.x + side * x / 24.f, min.y + side * y / 24.f); };
    const auto line = [&](float x, float y, float xx, float yy) { dl->AddLine(p(x, y), p(xx, yy), ink, 1.5f * dpi); };
    const auto rect = [&](float x, float y, float xx, float yy) { dl->AddRect(p(x, y), p(xx, yy), ink, 1.5f * dpi, 0, 1.5f * dpi); };
    switch (icon) {
    case Icon::Folder: rect(3, 7, 21, 20); line(3, 7, 3, 4); line(3, 4, 10, 4); line(10, 4, 13, 7); break;
    case Icon::Open: rect(5, 3, 19, 21); line(9, 12, 15, 12); line(12, 9, 15, 12); line(12, 15, 15, 12); break;
    case Icon::Refresh:
        dl->PathArcTo(p(12, 12), side * .34f, .5f, 5.9f, 18); dl->PathStroke(ink, 0, 1.5f * dpi);
        line(20, 5, 20, 10); line(15, 9, 20, 10); break;
    case Icon::Settings:
        dl->AddCircle(p(12, 12), side * .26f, ink, 16, 1.5f * dpi);
        dl->AddCircle(p(12, 12), side * .1f, ink, 12, 1.5f * dpi);
        for (int i = 0; i < 8; ++i) { const float a = i * 3.14159265f / 4.f;
            line(12 + 7 * std::cos(a), 12 + 7 * std::sin(a), 12 + 10 * std::cos(a), 12 + 10 * std::sin(a)); } break;
    case Icon::Sun:
        dl->AddCircle(p(12, 12), side * .22f, ink, 16, 1.5f * dpi);
        for (int i = 0; i < 8; ++i) { const float a = i * 3.14159265f / 4.f;
            line(12 + 8 * std::cos(a), 12 + 8 * std::sin(a), 12 + 10 * std::cos(a), 12 + 10 * std::sin(a)); } break;
    case Icon::Moon:
        dl->PathArcTo(p(12, 12), side * .38f, .2f, 4.5f, 20);
        dl->PathBezierCubicCurveTo(p(8, 13), p(13, 18), p(21, 14));
        dl->PathStroke(ink, ImDrawFlags_Closed, 1.5f * dpi); break;
    case Icon::Play: dl->AddTriangle(p(6, 3), p(6, 21), p(21, 12), ink, 1.5f * dpi); break;
    case Icon::Pause: rect(6, 4, 9, 20); rect(15, 4, 18, 20); break;
    case Icon::Back: line(12, 5, 4, 12); line(4, 12, 12, 19); line(21, 5, 13, 12); line(13, 12, 21, 19); break;
    case Icon::Forward: line(3, 5, 11, 12); line(11, 12, 3, 19); line(12, 5, 20, 12); line(20, 12, 12, 19); break;
    case Icon::Plus: line(12, 5, 12, 19); [[fallthrough]];
    case Icon::Minus: line(5, 12, 19, 12); break;
    case Icon::Left: line(15, 5, 8, 12); line(8, 12, 15, 19); break;
    case Icon::Right: line(9, 5, 16, 12); line(16, 12, 9, 19); break;
    case Icon::Close: line(6, 6, 18, 18); line(6, 18, 18, 6); break;
    case Icon::Keyboard:
        rect(2, 5, 22, 19);
        for (int y : {9, 12}) for (int x : {6, 10, 14, 18}) line(static_cast<float>(x), static_cast<float>(y), x + 1.f, static_cast<float>(y));
        line(7, 16, 17, 16); break;
    case Icon::Speaker:
    case Icon::Muted:
        line(3, 9, 7, 9); line(7, 9, 12, 5); line(12, 5, 12, 19);
        line(12, 19, 7, 15); line(7, 15, 3, 15); line(3, 15, 3, 9);
        if (icon == Icon::Muted) { line(16, 9, 22, 15); line(16, 15, 22, 9); }
        else { dl->PathArcTo(p(12, 12), side * .34f, -.7f, .7f, 8); dl->PathStroke(ink, 0, 1.5f * dpi); } break;
    case Icon::Solo:
        dl->PathArcTo(p(12, 12), side * .34f, 3.14159265f, 6.2831853f, 16); dl->PathStroke(ink, 0, 1.5f * dpi);
        rect(3, 12, 7, 20); rect(17, 12, 21, 20); break;
    case Icon::Piano:
        rect(2, 4, 22, 20); line(7, 5, 7, 19); line(12, 5, 12, 19); line(17, 5, 17, 19);
        dl->AddRectFilled(p(5, 4), p(9, 12), ink); dl->AddRectFilled(p(15, 4), p(19, 12), ink); break;
    }
}

bool IconButton(const char* id, Icon icon, const char* tip, const skin::Skin& s, float dpi, bool active = false) {
    const float height = s.metric.controlHeight;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool muted = icon == Icon::Muted;
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, Colour(muted ? s.accent.accentSoft : s.accent.okSoft));
    const bool clicked = ImGui::Button(id, ImVec2(height, height));
    if (active) ImGui::PopStyleColor();
    DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(min.x + (height - 16.f * dpi) / 2,
             min.y + (height - 16.f * dpi) / 2), 16.f * dpi,
             Colour(active ? (muted ? s.accent.accent : s.accent.okInk) : s.ink.secondary), dpi);
    if (active) { // Checked shape, in addition to semantic colour.
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(min.x + height - 8 * dpi, min.y + height - 5 * dpi),
                    ImVec2(min.x + height - 6 * dpi, min.y + height - 3 * dpi), Colour(s.ink.primary), dpi);
        dl->AddLine(ImVec2(min.x + height - 6 * dpi, min.y + height - 3 * dpi),
                    ImVec2(min.x + height - 2 * dpi, min.y + height - 8 * dpi), Colour(s.ink.primary), dpi);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tip);
    return clicked;
}

bool TransportButton(const char* id, Icon icon, const char* label, const skin::Skin& s, float dpi, bool primary = false) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float pad = (primary ? 16.f : 12.f) * dpi;
    const float width = 2 * pad + 18 * dpi + s.spacing.s2 + ImGui::CalcTextSize(label).x;
    const bool clicked = ImGui::Button(id, ImVec2(width, s.metric.controlHeight));
    auto* draw = ImGui::GetWindowDrawList();
    DrawIcon(draw, icon, ImVec2(min.x + pad, min.y + (s.metric.controlHeight - 16 * dpi) / 2),
             16 * dpi, ImGui::GetColorU32(ImGuiCol_Text), dpi);
    draw->AddText(ImVec2(min.x + pad + 24 * dpi, min.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2),
                  ImGui::GetColorU32(ImGuiCol_Text), label);
    return clicked;
}

// A six-pixel groove with a full mouse/keyboard hit target. ImGui owns drag,
// focus, navigation and clamping; only the presentation replaces its frame.
bool Groove(const char* id, float* value, float low, float high, float width, float height,
            const skin::Skin& s, float dpi, bool thumb) {
    const auto min = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, (height - ImGui::GetTextLineHeight()) / 2));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 0);
    for (const auto colour : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Border})
        ImGui::PushStyleColor(colour, IM_COL32(0, 0, 0, 0));
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::SliderFloat(id, value, low, high, "", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    ImGui::PopStyleColor(6); ImGui::PopStyleVar(2);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 track(min.x, min.y + (height - 6 * dpi) / 2);
    skin::RecessedRect(draw, track, ImVec2(track.x + width, track.y + 6 * dpi), 3 * dpi, s);
    const float fill = high > low ? std::clamp((*value - low) / (high - low), 0.f, 1.f) * width : 0;
    if (fill > 0) draw->AddRectFilled(track, ImVec2(track.x + fill, track.y + 6 * dpi), Colour(s.accent.accent), 3 * dpi);
    if (thumb || ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        const float x = std::clamp(track.x + fill, track.x + 6 * dpi, track.x + width - 6 * dpi);
        skin::RaisedRect(draw, ImVec2(x - 6 * dpi, track.y - 4 * dpi), ImVec2(x + 6 * dpi, track.y + 10 * dpi),
                         3 * dpi, s, Colour(s.surface.elevated));
    }
    return changed;
}

void DrawEllipsis(const std::string& text, float width, ImVec2 min) {
    const float height = ImGui::GetTextLineHeight();
    auto* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(min, ImVec2(min.x + std::max(1.f, width), min.y + height), true);
    const float measured = ImGui::CalcTextSize(text.c_str()).x;
    if (measured > width && width > ImGui::CalcTextSize("...").x) {
        const float available = width - ImGui::CalcTextSize("...").x;
        dl->PushClipRect(min, ImVec2(min.x + available, min.y + height), true);
        dl->AddText(min, ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
        dl->PopClipRect();
        dl->AddText(ImVec2(min.x + available, min.y), ImGui::GetColorU32(ImGuiCol_Text), "...");
    } else dl->AddText(min, ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
    dl->PopClipRect();
}

void Ellipsis(const std::string& text, float width) {
    const float height = ImGui::GetTextLineHeight();
    const float measured = ImGui::CalcTextSize(text.c_str()).x;
    DrawEllipsis(text, width, ImGui::GetCursorScreenPos());
    ImGui::Dummy(ImVec2(std::max(1.f, width), height));
    if (measured > width && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.c_str());
}

void BeginPanel(const char* id, ImVec2 min, ImVec2 max, const skin::Skin& s, ImGuiWindowFlags flags = 0) {
    skin::RaisedPanel(min, max, s);
    ImGui::SetCursorScreenPos(ImVec2(min.x + s.spacing.panelPad, min.y + s.spacing.panelPad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild(id, ImVec2(max.x - min.x - 2 * s.spacing.panelPad,
                     max.y - min.y - 2 * s.spacing.panelPad), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | flags);
    ImGui::PopStyleVar();
}

std::filesystem::path PickFile(HWND hwnd) {
    wchar_t filename[32768]{};
    OPENFILENAMEW open{sizeof(open)};
    open.hwndOwner = hwnd;
    open.lpstrFilter = L"MIDI files\0*.mid;*.midi\0All files\0*.*\0";
    open.lpstrFile = filename;
    open.nMaxFile = static_cast<DWORD>(std::size(filename));
    open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&open) ? std::filesystem::path(filename) : std::filesystem::path();
}

std::filesystem::path PickFolder(HWND hwnd) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    std::filesystem::path path;
    if (SUCCEEDED(dialog->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR text = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &text))) { path = text; CoTaskMemFree(text); }
            item->Release();
        }
    }
    dialog->Release();
    return path;
}

std::string Time(double seconds) {
    const int total = static_cast<int>(std::max(0.0, seconds));
    char text[32];
    snprintf(text, sizeof(text), "%d:%02d", total / 60, total % 60);
    return text;
}
}

void Panels::LoadPreferences(const std::filesystem::path& path) {
    try {
        std::ifstream stream(path);
        if (!stream) return;
        const auto json = nlohmann::json::parse(stream);
        preferences.skin = std::clamp(json.value("skin", 0), 0, 3);
        preferences.autoSolo = json.value("autoSoloPiano", true);
        preferences.keyMappingOpen = json.value("keyMappingOpen", true);
        const auto folder = json.value("midiFolder", std::string());
        preferences.folder = std::filesystem::path(std::u8string(folder.begin(), folder.end()));
    } catch (const std::exception&) { preferences = {}; }
}

void Panels::SavePreferences(const std::filesystem::path& path) const {
    nlohmann::json json{{"skin", preferences.skin}, {"autoSoloPiano", preferences.autoSolo},
                        {"midiFolder", Utf8(preferences.folder)}, {"keyMappingOpen", preferences.keyMappingOpen}};
    std::ofstream stream(path);
    if (stream) stream << json.dump(2) << '\n';
}

void Panels::DrawKeyMapping(const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    const bool platform = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    if (platform && mappingDpi_ > 0) dpi = mappingDpi_;
    const bool modern = design.type.family == "IBM Plex Sans";
    const float height = modern ? 268.390625f : 246.9375f;
    const auto* main = ImGui::GetMainViewport();
    if (!platform || mappingDpi_ == 0)
        ImGui::SetNextWindowPos(ImVec2(main->Pos.x + 125 * dpi, main->Pos.y + main->Size.y - (height + 40) * dpi));
    ImGui::SetNextWindowSize(ImVec2(840 * dpi, height * dpi));
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    const bool visible = ImGui::Begin("Key Mapping", &preferences.keyMappingOpen,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    if (!visible) { ImGui::End(); return; }
    if (platform) dpi = ImGui::GetWindowViewport()->DpiScale;
    mappingDpi_ = dpi;
    const ImGuiStyle previousStyle = ImGui::GetStyle();
    skin::ApplyStyle(design, dpi);
    const auto s = skin::ScaleGeometry(design, dpi);
    ImGui::PushFont(fonts.Get(design), design.type.body * SpecFontScale(design));
    const auto state = engine.Snapshot();
    const auto origin = ImGui::GetWindowPos();
    auto* draw = ImGui::GetWindowDrawList();
    const float title = (modern ? 44.f : 36.f) * dpi, width = 840 * dpi, pad = s.spacing.panelPad;
    const auto at = [&](float x, float y) { return ImVec2(origin.x + x, origin.y + y); };
    draw->AddRectFilled(origin, at(width, height * dpi), Colour(s.surface.canvas), s.radius.window);
    draw->AddRectFilled(origin, at(width, title), Colour(s.surface.structure), s.radius.window, ImDrawFlags_RoundCornersTop);
    draw->AddLine(at(0, title), at(width, title), Colour(s.border.hairline), dpi);
    draw->AddText(at(pad, (title - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.primary), "Key Mapping");
    ImGui::SetCursorScreenPos(at(width - 8 * dpi - s.metric.controlHeight, (title - s.metric.controlHeight) / 2));
    if (IconButton("##close-mapping", Icon::Close, "Close key mapping", s, dpi)) {
        preferences.keyMappingOpen = false; mappingArmed_ = false;
    }
    const float rowY = title + pad;
    std::string readout = "Click a key to remap";
    if (selectedNote_ >= 0) {
        const auto found = state->keyMappings.find(NoteName(selectedNote_));
        readout = NoteName(selectedNote_) + (mappingArmed_ ? ": press a key to assign (Esc cancels)" :
            " is typed as " + (found == state->keyMappings.end() || found->second.empty() ? "nothing" : found->second));
    }
    { FontScope font(fonts, design, design.type.meta * SpecFontScale(design));
      draw->AddText(at(pad, rowY + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.secondary), readout.c_str()); }
    const float fullWidth = ImGui::CalcTextSize("Full 88").x + 24 * dpi;
    ImGui::SetCursorScreenPos(at(width - pad - 2 * s.metric.controlHeight - fullWidth - 16 * dpi, rowY));
    ImGui::BeginDisabled(fullKeyboard_ || rangeStart_ <= 21);
    if (IconButton("##lower-range", Icon::Left, "Lower range", s, dpi)) { rangeStart_ = std::max(21, rangeStart_ - 12); mappingArmed_ = false; }
    ImGui::EndDisabled(); ImGui::SameLine();
    const bool wasFull = fullKeyboard_;
    if (wasFull) ImGui::PushStyleColor(ImGuiCol_Button, Colour(s.accent.accentSoft));
    if (ImGui::Button("Full 88", ImVec2(fullWidth, s.metric.controlHeight))) { fullKeyboard_ = !fullKeyboard_; mappingArmed_ = false; }
    if (wasFull) ImGui::PopStyleColor();
    if (fullKeyboard_) {
        // A checked corner distinguishes the selected mode without colour.
        const auto max = ImGui::GetItemRectMax();
        draw->AddLine(ImVec2(max.x - 9 * dpi, max.y - 6 * dpi), ImVec2(max.x - 6 * dpi, max.y - 3 * dpi), Colour(s.ink.primary), dpi);
        draw->AddLine(ImVec2(max.x - 6 * dpi, max.y - 3 * dpi), ImVec2(max.x - 2 * dpi, max.y - 9 * dpi), Colour(s.ink.primary), dpi);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(fullKeyboard_ || rangeStart_ >= 60);
    if (IconButton("##higher-range", Icon::Right, "Higher range", s, dpi)) { rangeStart_ = std::min(60, rangeStart_ + 12); mappingArmed_ = false; }
    ImGui::EndDisabled();

    const float frameY = rowY + s.metric.controlHeight + 12 * dpi;
    skin::RecessedRect(draw, at(pad, frameY), at(width - pad, frameY + 114 * dpi), s.radius.element, s);
    const float pianoX = pad + 9 * dpi, pianoY = frameY + 9 * dpi, pianoWidth = width - 2 * pad - 18 * dpi;
    draw->AddRectFilled(at(pianoX, pianoY), at(pianoX + pianoWidth, pianoY + 96 * dpi), IM_COL32(29, 27, 24, 255), 3 * dpi);
    const auto white = [](int note) { const int n = note % 12; return n != 1 && n != 3 && n != 6 && n != 8 && n != 10; };
    std::vector<int> whites;
    for (int note = fullKeyboard_ ? 21 : rangeStart_; note <= 108 && whites.size() < (fullKeyboard_ ? 52u : 29u); ++note)
        if (white(note)) whites.push_back(note);
    const float whiteWidth = (pianoWidth - 8 * dpi) / static_cast<float>(whites.size());
    struct Key { int note; ImVec2 min, max; bool black; };
    std::vector<Key> keys;
    for (size_t i = 0; i < whites.size(); ++i) {
        const float x = pianoX + 4 * dpi + i * whiteWidth;
        keys.push_back({whites[i], at(x, pianoY + 4 * dpi), at(x + whiteWidth, pianoY + 96 * dpi), false});
    }
    for (size_t i = 0; i + 1 < whites.size(); ++i) {
        if (white(whites[i] + 1)) continue;
        const float x = pianoX + 4 * dpi + (i + 1) * whiteWidth - whiteWidth * .31f;
        keys.push_back({whites[i] + 1, at(x, pianoY + 4 * dpi), at(x + whiteWidth * .62f, pianoY + 57 * dpi), true});
    }
    ImGui::SetCursorScreenPos(at(pianoX, pianoY));
    ImGui::InvisibleButton("##piano", ImVec2(pianoWidth, 96 * dpi));
    int hoveredNote = -1;
    if (ImGui::IsItemHovered()) {
        const auto mouse = ImGui::GetIO().MousePos;
        // Black keys are last, so they take precedence over the whites beneath.
        for (const auto& key : keys)
            if (mouse.x >= key.min.x && mouse.x < key.max.x && mouse.y >= key.min.y && mouse.y < key.max.y) hoveredNote = key.note;
        if (hoveredNote >= 0) {
            const auto found = state->keyMappings.find(NoteName(hoveredNote));
            ImGui::SetTooltip("%s: %s", NoteName(hoveredNote).c_str(), found == state->keyMappings.end() ? "Unassigned" : found->second.c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                selectedNote_ = hoveredNote; mappingArmed_ = true;
                engine.Send({ShellEngine::Action::Pause, {}, state->generation});
            }
        }
    }
    { FontScope font(fonts, design, 9.f * SpecFontScale(design));
      for (const auto& key : keys) {
        const ImU32 top = key.black ? IM_COL32(49, 47, 44, 255) : IM_COL32(255, 254, 251, 255);
        const ImU32 bottom = key.black ? IM_COL32(22, 21, 19, 255) : IM_COL32(242, 238, 229, 255);
        if (key.black) draw->AddRectFilled(ImVec2(key.min.x, key.min.y + 2 * dpi), ImVec2(key.max.x + dpi, key.max.y + 3 * dpi), IM_COL32(0, 0, 0, 60), 2 * dpi);
        draw->AddRectFilledMultiColor(key.min, key.max, top, top, bottom, bottom);
        draw->AddRect(key.min, key.max, key.black ? IM_COL32(0, 0, 0, 180) : IM_COL32(101, 87, 63, 100), 2 * dpi, 0, dpi);
        if (key.note == selectedNote_) {
            draw->AddRect(key.min, key.max, Colour(s.accent.accent), 2 * dpi, 0, 2 * dpi);
            // Selection has a notch as well as its themed outline.
            draw->AddTriangleFilled(ImVec2(key.min.x + 2 * dpi, key.min.y + 2 * dpi),
                ImVec2(key.min.x + 8 * dpi, key.min.y + 2 * dpi), ImVec2(key.min.x + 2 * dpi, key.min.y + 8 * dpi),
                key.black ? IM_COL32_WHITE : IM_COL32_BLACK);
        }
        const auto found = state->keyMappings.find(NoteName(key.note));
        if (!fullKeyboard_ && found != state->keyMappings.end()) {
            std::string label = found->second;
            const bool ctrl = label.starts_with("ctrl+");
            if (ctrl) label.erase(0, 5);
            const auto ink = key.black ? IM_COL32(245, 239, 226, 255) : IM_COL32(51, 47, 41, 255);
            const float center = (key.min.x + key.max.x) / 2;
            const float y = key.max.y - 8 * dpi - ImGui::GetTextLineHeight();
            draw->PushClipRect(key.min, key.max, true);
            draw->AddText(ImVec2(center - ImGui::CalcTextSize(label.c_str()).x / 2, y), ink, label.c_str());
            if (ctrl) draw->AddText(ImVec2(center - ImGui::CalcTextSize("Ctrl").x / 2, y - 10 * dpi), ink, "Ctrl");
            draw->PopClipRect();
        }
      }
    }
    if (mappingArmed_ && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        auto& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) mappingArmed_ = false;
        else if (!io.KeyAlt && !io.KeySuper) {
            std::string key;
            if (io.KeyCtrl) {
                for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k)
                    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) key = std::string("ctrl+") + static_cast<char>((io.KeyShift ? 'A' : 'a') + k - ImGuiKey_A);
                for (int k = ImGuiKey_0; k <= ImGuiKey_9; ++k)
                    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) key = std::string("ctrl+") + static_cast<char>('0' + k - ImGuiKey_0);
            } else for (ImWchar c : io.InputQueueCharacters)
                if (c > 32 && c < 127) { key = static_cast<char>(c); break; }
            if (!key.empty()) {
                engine.Send({ShellEngine::Action::Remap, {}, 0, static_cast<size_t>(selectedNote_), false, 0, key});
                mappingArmed_ = false;
            }
        }
    } else if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) mappingArmed_ = false;
    const float statusY = frameY + 114 * dpi + pad;
    draw->AddRectFilled(at(0, statusY), at(width, height * dpi), Colour(s.surface.structure));
    draw->AddLine(at(0, statusY), at(width, statusY), Colour(s.border.hairline), dpi);
    ImGui::SetCursorScreenPos(at(pad, statusY + 8 * dpi));
    { FontScope font(fonts, design, design.type.meta * SpecFontScale(design));
      const std::string status = !state->error.empty() ? state->error :
          "Layout 88-key     Range " + NoteName(whites.front()) + "-" + NoteName(whites.back());
      Ellipsis(status, width - 2 * pad); }
    ImGui::PopFont();
    ImGui::GetStyle() = previousStyle;
    ImGui::End();
}

void Panels::Draw(HWND hwnd, const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    const auto s = skin::ScaleGeometry(design, dpi);
    auto state = engine.Snapshot();
    const auto send = [&](ShellEngine::Action action, size_t track = 0, bool value = false) {
        engine.Send({action, {}, state->generation, track, value});
    };
    const auto load = [&](const std::filesystem::path& path) {
        if (!path.empty()) engine.Send({ShellEngine::Action::Load, path, 0, 0, preferences.autoSolo});
    };

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    auto* dl = ImGui::GetWindowDrawList();
    const float strip = 81 * dpi, status = 28 * dpi;
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + strip), Colour(s.surface.structure));
    dl->AddLine(ImVec2(origin.x, origin.y + strip), ImVec2(origin.x + size.x, origin.y + strip), Colour(s.border.hairline));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + s.spacing.windowPad, origin.y + s.spacing.s2));
    { FontScope font(fonts, design, design.type.title, Weight::Semibold); ImGui::TextUnformatted("MIDI++"); }
    ImGui::SameLine(0, s.spacing.s3);
    { FontScope font(fonts, design, design.type.meta); ImGui::TextDisabled("Autoplay"); }

    ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - s.spacing.windowPad - 3 * s.metric.controlHeight - 2 * s.spacing.s2,
                                    origin.y + s.spacing.s2));
    if (IconButton("##key-mapping", Icon::Keyboard, "Key Mapping", s, dpi, preferences.keyMappingOpen))
        preferences.keyMappingOpen = !preferences.keyMappingOpen;
    ImGui::SameLine();
    if (IconButton("##theme", s.dark ? Icon::Moon : Icon::Sun, s.dark ? "Switch to light" : "Switch to dark", s, dpi))
        preferences.skin ^= 1;
    ImGui::SameLine();
    if (IconButton("##settings", Icon::Settings, "Settings", s, dpi)) ImGui::OpenPopup("Settings");
    if (ImGui::BeginPopup("Settings")) {
        ImGui::TextUnformatted("Appearance");
        int family = preferences.skin / 2;
        if (ImGui::RadioButton("Classic", family == 0)) { preferences.skin %= 2; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::RadioButton("Modern", family == 1)) { preferences.skin = 2 + preferences.skin % 2; ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        ImGui::Checkbox("Solo piano tracks on load", &preferences.autoSolo);
        if (ImGui::CollapsingHeader("About")) {
            ImGui::TextUnformatted("Based on Zephkek/MIDIPlusPlus (GPLv3)");
            ImGui::TextUnformatted("Dear ImGui and RtMidi (MIT)");
            ImGui::TextUnformatted("IBM Plex Sans (SIL Open Font License 1.1)");
        }
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x + s.spacing.windowPad, origin.y + strip - s.metric.controlHeight - s.spacing.s2));
    bool velocity = state->velocity, sustain = state->sustain;
    if (ImGui::Checkbox("Velocity", &velocity)) send(ShellEngine::Action::Velocity, 0, velocity);
    ImGui::SameLine();
    ImGui::BeginDisabled(state->playing);
    if (ImGui::Checkbox("Sustain", &sustain)) send(ShellEngine::Action::Sustain, 0, sustain);
    ImGui::EndDisabled();
    ImGui::SameLine(0, s.spacing.s4);
    ImGui::TextDisabled("88 keys");

    const float top = origin.y + strip + s.spacing.windowPad;
    const float bottom = origin.y + size.y - status - s.spacing.windowPad;
    const float leftWidth = std::min(336 * dpi, size.x * .33f);
    const ImVec2 leftMin(origin.x + s.spacing.windowPad, top);
    const ImVec2 leftMax(leftMin.x + leftWidth, bottom);
    BeginPanel("Files", leftMin, leftMax, s);
    { FontScope font(fonts, design, design.type.heading, Weight::Semibold); ImGui::TextUnformatted("MIDI files"); }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 3 * s.metric.controlHeight - 2 * s.spacing.s2);
    if (IconButton("##open", Icon::Open, "Open MIDI file", s, dpi)) load(PickFile(hwnd));
    ImGui::SameLine();
    ImGui::BeginDisabled(state->playing || state->busy);
    if (IconButton("##folder", Icon::Folder, "Choose MIDI folder", s, dpi)) {
        const auto path = PickFolder(hwnd);
        if (!path.empty()) { preferences.folder = path; engine.Send({ShellEngine::Action::Scan, path}); }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state->folder.empty());
    if (IconButton("##refresh", Icon::Refresh, "Refresh MIDI folder", s, dpi)) engine.Send({ShellEngine::Action::Scan, state->folder});
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search MIDI files", search_, sizeof(search_));
    const ImVec2 listMin = ImGui::GetCursorScreenPos();
    const ImVec2 listSize = ImGui::GetContentRegionAvail();
    skin::RecessedField(listMin, ImVec2(listMin.x + listSize.x, listMin.y + listSize.y), s);
    ImGui::BeginChild("##file-list", listSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (state->files->empty()) {
        ImGui::TextWrapped("Open a MIDI file or choose its folder.");
    } else {
        std::string query(search_);
        const auto lowercase = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        };
        query = lowercase(query);
        if (filteredFiles_ != state->files || filteredQuery_ != query) {
            fileFilter_.clear();
            for (size_t i = 0; i < state->files->size(); ++i)
                if (query.empty() || lowercase((*state->files)[i].name).find(query) != std::string::npos) fileFilter_.push_back(i);
            filteredFiles_ = state->files;
            filteredQuery_ = query;
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(fileFilter_.size()), s.metric.controlHeight + s.spacing.s2);
        while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& file = (*state->files)[fileFilter_[i]];
            ImGui::PushID(static_cast<int>(fileFilter_[i]));
            const auto pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::BeginDisabled(state->busy);
            if (ImGui::Selectable("##file", file.path == state->loaded, 0, ImVec2(width, s.metric.controlHeight))) load(file.path);
            ImGui::EndDisabled();
            DrawEllipsis(file.name, width - 2 * s.spacing.s2,
                         ImVec2(pos.x + s.spacing.s2, pos.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", file.name.c_str());
            ImGui::PopID();
        }
        if (fileFilter_.empty()) ImGui::TextDisabled("No matching files");
    }
    ImGui::EndChild();
    ImGui::EndChild();

    const float right = leftMax.x + s.spacing.s3;
    const float edge = origin.x + size.x - s.spacing.windowPad;
    const bool modern = design.type.family == "IBM Plex Sans";
    // Measured from the rendered mockup: 18.75/25 title line, 12px gaps,
    // 6px seek, two 28/32px rows and 12/16px card padding.
    const float titleHeight = (modern ? 25.f : 18.75f) * dpi;
    const float playbackHeight = 2 * s.spacing.panelPad + titleHeight + 42 * dpi + 2 * s.metric.controlHeight;
    BeginPanel("Playback", ImVec2(right, top), ImVec2(edge, top + playbackHeight), s,
               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushFont(fonts.Get(design), design.type.body * SpecFontScale(design));
    const auto content = ImGui::GetCursorScreenPos();
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    { FontScope font(fonts, design, (modern ? 20.f : 15.f) * SpecFontScale(design), Weight::Medium);
      DrawEllipsis(state->loaded.empty() ? "Playback" : Utf8(state->loaded.stem()), contentWidth, content); }
    const auto number = [&](ShellEngine::Action action, double amount) {
        engine.Send({action, {}, state->generation, 0, false, amount});
    };
    ImGui::BeginDisabled(state->loaded.empty() || state->rows.empty() || state->busy);
    ImGui::SetCursorScreenPos(ImVec2(content.x, content.y + titleHeight + 4 * dpi));
    if (!seeking_ || seekGeneration_ != state->generation) {
        seekPosition_ = static_cast<float>(state->position);
        seeking_ = false;
    }
    const bool seekChanged = Groove("##seek", &seekPosition_, 0, static_cast<float>(std::max(.001, state->duration)),
                                     contentWidth, 22 * dpi, s, dpi, false);
    if (ImGui::IsItemActivated()) { seeking_ = true; seekGeneration_ = state->generation; }
    if (seeking_ && ImGui::IsItemDeactivatedAfterEdit()) {
        if (seekGeneration_ == state->generation) number(ShellEngine::Action::Seek, seekPosition_);
        seeking_ = false;
    } else if (seekChanged && !ImGui::IsItemActive()) number(ShellEngine::Action::Seek, seekPosition_);
    if (ImGui::IsItemHovered() || seeking_) ImGui::SetTooltip("%s", Time(seekPosition_).c_str());
    const float transportY = content.y + titleHeight + 30 * dpi;
    ImGui::SetCursorScreenPos(ImVec2(content.x, transportY));
    if (TransportButton("##play", state->playing ? Icon::Pause : Icon::Play, state->playing ? "Pause" : "Play", s, dpi, true))
        send(ShellEngine::Action::TogglePlayPause);
    ImGui::SameLine();
    if (TransportButton("##restart", Icon::Refresh, "Restart", s, dpi)) send(ShellEngine::Action::Restart);
    ImGui::SameLine();
    if (TransportButton("##back10", Icon::Back, "-10s", s, dpi)) send(ShellEngine::Action::Back10);
    ImGui::SameLine();
    if (TransportButton("##forward10", Icon::Forward, "+10s", s, dpi)) send(ShellEngine::Action::Forward10);
    const std::string time = Time(seeking_ ? seekPosition_ : state->position) + " / " + Time(state->duration);
    dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(content.x + contentWidth - ImGui::CalcTextSize(time.c_str()).x,
                transportY + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.secondary), time.c_str());
    ImGui::SetCursorScreenPos(ImVec2(content.x, transportY + s.metric.controlHeight + 12 * dpi));
    const auto label = [&](const char* text) {
        FontScope font(fonts, design, design.type.meta * SpecFontScale(design));
        const auto pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::CalcTextSize(text).x;
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, pos.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.secondary), text);
        ImGui::Dummy(ImVec2(width, s.metric.controlHeight)); ImGui::SameLine();
    };
    label("Speed");
    ImGui::BeginDisabled(state->speed <= .25);
    if (IconButton("##slower", Icon::Minus, "Slower", s, dpi)) number(ShellEngine::Action::Speed, state->speed - .05);
    ImGui::EndDisabled(); ImGui::SameLine();
    const auto speedMin = ImGui::GetCursorScreenPos();
    char speed[32]; snprintf(speed, sizeof(speed), "%.2f\xc3\x97", state->speed);
    dl->AddText(ImVec2(speedMin.x + (56 * dpi - ImGui::CalcTextSize(speed).x) / 2,
                      speedMin.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.primary), speed);
    ImGui::Dummy(ImVec2(56 * dpi, s.metric.controlHeight)); ImGui::SameLine();
    ImGui::BeginDisabled(state->speed >= 2);
    if (IconButton("##faster", Icon::Plus, "Faster", s, dpi)) number(ShellEngine::Action::Speed, state->speed + .05);
    ImGui::EndDisabled(); ImGui::SameLine();
    label("Transpose");
    float transpose = static_cast<float>(state->transpose);
    if (Groove("##transpose", &transpose, -12, 12, 160 * dpi, s.metric.controlHeight, s, dpi, true))
        number(ShellEngine::Action::Transpose, std::round(transpose));
    ImGui::SameLine();
    const auto transposeMin = ImGui::GetCursorScreenPos();
    char transposeText[16]; snprintf(transposeText, sizeof(transposeText), "%+d", static_cast<int>(std::round(transpose)));
    dl->AddText(ImVec2(transposeMin.x + 32 * dpi - ImGui::CalcTextSize(transposeText).x,
                       transposeMin.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.primary), transposeText);
    ImGui::Dummy(ImVec2(32 * dpi, s.metric.controlHeight));
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();

    const float trackTop = top + playbackHeight + s.spacing.s3;
    BeginPanel("Tracks", ImVec2(right, trackTop), ImVec2(edge, bottom), s);
    { FontScope font(fonts, design, design.type.heading, Weight::Semibold); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Tracks"); }
    const float actionsWidth = ImGui::CalcTextSize("Solo Piano").x + ImGui::CalcTextSize("Unmute All").x + 4 * ImGui::GetStyle().FramePadding.x + s.spacing.s2;
    ImGui::SameLine(ImGui::GetWindowWidth() - actionsWidth);
    ImGui::BeginDisabled(state->rows.empty() || state->busy);
    if (ImGui::Button("Solo Piano")) send(ShellEngine::Action::SoloPiano);
    ImGui::SameLine();
    if (ImGui::Button("Unmute All")) send(ShellEngine::Action::UnmuteAll);
    ImGui::EndDisabled();
    const ImVec2 tableMin = ImGui::GetCursorScreenPos();
    const ImVec2 tableSize = ImGui::GetContentRegionAvail();
    skin::RecessedField(tableMin, ImVec2(tableMin.x + tableSize.x, tableMin.y + tableSize.y), s);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(s.spacing.s2, s.spacing.s1));
    if (ImGui::BeginTable("##tracks", 7, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_SizingStretchProp, tableSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 20 * dpi);
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Instrument", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("Ch", ImGuiTableColumnFlags_WidthFixed, 28 * dpi);
        ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthFixed, 48 * dpi);
        ImGui::TableSetupColumn("Mute", ImGuiTableColumnFlags_WidthFixed, s.metric.controlHeight);
        ImGui::TableSetupColumn("Solo", ImGuiTableColumnFlags_WidthFixed, s.metric.controlHeight);
        { FontScope font(fonts, design, design.type.meta, Weight::Semibold); ImGui::TableHeadersRow(); }
        const bool anySolo = AnySolo(state->rows);
        ImGuiListClipper tracks;
        tracks.Begin(static_cast<int>(state->rows.size()), s.metric.controlHeight + 2 * s.spacing.s1);
        while (tracks.Step()) for (int rowIndex = tracks.DisplayStart; rowIndex < tracks.DisplayEnd; ++rowIndex) {
            const auto& row = state->rows[rowIndex];
            const bool audible = TrackAudible(row, anySolo);
            ImGui::PushID(static_cast<int>(row.index));
            ImGui::TableNextRow(0, s.metric.controlHeight + 2 * s.spacing.s1);
            ImGui::PushStyleColor(ImGuiCol_Text, Colour(audible ? s.ink.primary : s.ink.tertiary));
            if (!audible) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, Colour(s.surface.recessed));
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::Text("%zu", row.index + 1);
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
            { FontScope font(fonts, design, design.type.body, audible ? Weight::Medium : Weight::Regular);
              Ellipsis(row.name, ImGui::GetContentRegionAvail().x); }
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
            if (row.piano) {
                const auto p = ImGui::GetCursorScreenPos();
                DrawIcon(ImGui::GetWindowDrawList(), Icon::Piano, p, 14 * dpi, Colour(s.ink.secondary), dpi);
                ImGui::Dummy(ImVec2(14 * dpi, ImGui::GetTextLineHeight())); ImGui::SameLine(0, s.spacing.s1);
            }
            Ellipsis(row.instrument, ImGui::GetContentRegionAvail().x);
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); Ellipsis(row.channels, ImGui::GetContentRegionAvail().x);
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::Text("%zu", row.notes);
            ImGui::PopStyleColor();
            ImGui::BeginDisabled(state->busy);
            ImGui::TableNextColumn();
            if (IconButton("##mute", row.muted ? Icon::Muted : Icon::Speaker,
                           row.muted ? "Unmute track" : "Mute track", s, dpi, row.muted)) send(ShellEngine::Action::Mute, row.index, !row.muted);
            ImGui::TableNextColumn();
            if (IconButton("##solo", Icon::Solo, row.solo ? "Clear solo" : "Solo track", s, dpi, row.solo))
                send(ShellEngine::Action::Solo, row.index, !row.solo);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (state->rows.empty()) {
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(state->loaded.empty() ? "Open a MIDI file" : "No note tracks");
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    const ImVec2 statusMin(origin.x, origin.y + size.y - status);
    dl->AddRectFilled(statusMin, ImVec2(origin.x + size.x, origin.y + size.y), Colour(s.surface.structure));
    ImGui::SetCursorScreenPos(ImVec2(statusMin.x + s.spacing.windowPad, statusMin.y + (status - design.type.meta * dpi) / 2));
    { FontScope font(fonts, design, design.type.meta);
      if (!state->error.empty()) Ellipsis(state->error, size.x - 2 * s.spacing.windowPad);
      else if (state->busy) ImGui::TextUnformatted("Loading...");
      else if (!state->rows.empty()) ImGui::Text("%zu of %zu tracks silent", SilentTracks(state->rows), state->rows.size());
      else ImGui::Text("%zu MIDI files", state->files->size()); }
    if (preferences.keyMappingOpen) DrawKeyMapping(fonts, design, dpi, engine);
    else mappingArmed_ = false;
}
}
