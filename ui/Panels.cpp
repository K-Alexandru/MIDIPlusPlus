#include "Panels.hpp"
#include "IconData.hpp"
#include "imgui_internal.h"
#include "json.hpp"
#include <commdlg.h>
#include <shobjidl.h>
#include <fstream>
#include <cmath>
#include "MidiInput.hpp"

namespace shell {
namespace {
ImU32 Colour(skin::Argb c) { return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, (c >> 24) & 255); }
ImU32 OpaqueTint(skin::Argb tint, skin::Argb surface) {
    const unsigned alpha = tint >> 24;
    const auto channel = [&](int shift) { return (((tint >> shift) & 255) * alpha +
        ((surface >> shift) & 255) * (255 - alpha) + 127) / 255; };
    return IM_COL32(channel(16), channel(8), channel(0), 255);
}
// Browser CSS sizes use the font em. stb_truetype uses ascent minus descent.
// Ratios from the shipped hhea/head tables: Segoe 2724/2048, Plex 1300/1000.
// Apply this correction wherever typography is measured against the spec.
float SpecFontScale(const skin::Skin& s) { return s.type.family == "IBM Plex Sans" ? 1.3f : 2724.f / 2048.f; }
enum class Icon { Folder, Open, Refresh, Settings, Sun, Moon, Play, Pause, Back, Forward,
                  Minus, Plus, Left, Right, Down, Up, Close, Keyboard, Speaker, Muted, Solo, Piano,
                  Mini, Expand, Copy, Rename, Check, SortDown, SortUp };

// Icons are Lucide, flattened to polylines by tools/gen-icons.py into
// ui/IconData.hpp. They used to be hand-written primitives here, which is how
// the piano ended up a filled blob and the gear read as a sun at strip size.
// Vector rather than a rasterised atlas so 150 and 200 percent stay crisp, and
// not an icon font because the house rules rule out font glyphs.
void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 min, float side, ImU32 ink, float dpi) {
    const auto& glyph = icon_data::kGlyphs[static_cast<int>(icon)];
    const float scale = side / (icon_data::kGrid * icon_data::kUnit);
    // Lucide is drawn at stroke-width 2 on a 24 grid. Below one pixel a stroke
    // stops being a line and starts being a grey smear, so it is clamped.
    const float thickness = std::max(1.f, side * icon_data::kStrokeWidth / icon_data::kGrid);
    for (unsigned short p = 0; p < glyph.count; ++p) {
        const auto& path = icon_data::kPaths[glyph.first + p];
        for (unsigned short i = 0; i < path.count; ++i) {
            const short* xy = &icon_data::kPoints[2 * (path.first + i)];
            dl->PathLineTo(ImVec2(min.x + xy[0] * scale, min.y + xy[1] * scale));
        }
        dl->PathStroke(ink, path.closed ? ImDrawFlags_Closed : 0, thickness);
    }
}

bool IconButton(const char* id, Icon icon, const char* tip, const skin::Skin& s, float dpi, bool active = false) {
    const float height = s.metric.controlHeight;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, Colour(s.accent.accentSoft));
    const bool clicked = ImGui::Button(id, ImVec2(height, height));
    if (active) ImGui::PopStyleColor();
    DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(min.x + (height - 16.f * dpi) / 2,
             min.y + (height - 16.f * dpi) / 2), 16.f * dpi,
             Colour(active ? s.accent.accent : s.ink.secondary), dpi);
    if (active) { // Shape as well as colour, per the house rules.
        const float inset = 6 * dpi;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x + inset, min.y + height - 4 * dpi),
                                           ImVec2(min.x + height - inset, min.y + height - 4 * dpi),
                                           Colour(s.accent.accent), 2 * dpi);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tip);
    return clicked;
}

// Icon and label are centred as one unit. The width used to reserve 18px plus
// a gap for the icon while the label was drawn 24px in, which left every
// transport button two pixels wider on the right than on the left.
bool TransportBody(const char* id, const Icon* icon, const char* label,
                   const skin::Skin& s, float dpi, bool primary) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float pad = (primary ? 16.f : 12.f) * dpi;
    const float side = 16 * dpi;
    const float lead = icon ? side + s.spacing.s2 : 0.f;
    const float width = 2 * pad + lead + ImGui::CalcTextSize(label).x;
    const bool clicked = ImGui::Button(id, ImVec2(width, s.metric.controlHeight));
    auto* draw = ImGui::GetWindowDrawList();
    if (icon)
        DrawIcon(draw, *icon, ImVec2(min.x + pad, min.y + (s.metric.controlHeight - side) / 2),
                 side, ImGui::GetColorU32(ImGuiCol_Text), dpi);
    draw->AddText(ImVec2(min.x + pad + lead, min.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2),
                  ImGui::GetColorU32(ImGuiCol_Text), label);
    return clicked;
}

bool TransportButton(const char* id, Icon icon, const char* label, const skin::Skin& s,
                     float dpi, bool primary = false) {
    return TransportBody(id, &icon, label, s, dpi, primary);
}

// Label only, for the seek buttons. The spec draws them as bare text: a
// chevron beside "10s" is the same word twice, and it crowded the pill.
bool TransportButton(const char* id, const char* label, const skin::Skin& s,
                     float dpi, bool primary = false) {
    return TransportBody(id, nullptr, label, s, dpi, primary);
}

// ImGui's combo arrow is a heavy filled triangle. Every other mark in the
// shell is a Lucide stroke, so the arrow is suppressed and one drawn here.
// Sized off the combo's own height rather than a passed skin, so it can be
// called from helpers that were never given one.
void ComboChevron() {
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const float height = max.y - min.y;
    const float side = height * .5f;
    DrawIcon(ImGui::GetWindowDrawList(), Icon::Down,
             ImVec2(max.x - side - height * .28f, min.y + (height - side) / 2),
             side, ImGui::GetColorU32(ImGuiCol_Text), 1.f);
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
    auto min = ImGui::GetCursorScreenPos();
    const float baseline = ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
    min.y += baseline;
    DrawEllipsis(text, width, min);
    ImGui::Dummy(ImVec2(std::max(1.f, width), height + baseline));
    if (measured > width && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.c_str());
}

bool StatePill(const char* label, bool on, const Fonts& fonts, const skin::Skin& design,
               float dpi, float padding, bool enabled = true, const char* tip = nullptr) {
    auto s = skin::ScaleGeometry(design, dpi);
    FontScope font(fonts, design, design.type.body * SpecFontScale(design), on ? Weight::Semibold : Weight::Regular);
    const auto min = ImGui::GetCursorScreenPos();
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 size(text.x + 2 * padding + 2 * dpi, s.metric.controlHeight);
    if (on) s.border.hairline = s.accent.okBorder;

    // One drawing path for every pill, interactive or not. Splitting them
    // between ImGui::Button and a hand-placed AddText put the semibold on
    // labels two pixels lower than the regular off ones, which read as the
    // text sitting unevenly inside the pill.
    bool clicked = false;
    if (enabled) clicked = ImGui::InvisibleButton(label, size);
    else ImGui::Dummy(size);
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();

    ImU32 fill = on ? OpaqueTint(s.accent.okSoft, s.surface.structure) : Colour(s.surface.elevated);
    if (held) fill = Colour(s.surface.recessed);
    else if (hovered) fill = Colour(on ? s.accent.okSoft : s.surface.elevatedHot);
    // CSS clips the outside shadow at the control edge. Composite the tint
    // first so our stacked shadow cannot darken the translucent on surface.
    auto* dl = ImGui::GetWindowDrawList();
    skin::RaisedRect(dl, min, ImVec2(min.x + size.x, min.y + size.y), s.radius.control, s, fill);
    dl->AddText(ImVec2(min.x + (size.x - text.x) / 2, min.y + (size.y - text.y) / 2),
                Colour(on ? s.accent.okInk : s.ink.primary), label);

    // No underline under the label: at pill width it read as text decoration
    // rather than as a state. The on state is still not carried by colour
    // alone, because an on pill is semibold where an off pill is regular.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s: %s%s%s", label, on ? "On" : "Off", tip ? "\n" : "", tip ? tip : "");
    return clicked;
}

void StatePills(const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine, bool compact) {
    const auto state = engine.Snapshot();
    const float pad = (compact ? 8.f : 12.f) * dpi;
    if (StatePill("Midi2Key", state->liveActive, fonts, design, dpi, compact ? pad : 16 * dpi,
                  !state->liveDevice.empty(), state->liveDevice.empty() ? "Select a MIDI input in Settings." : nullptr))
        engine.Send({ShellEngine::Action::LiveActive, {}, 0, 0, !state->liveActive});
    ImGui::SameLine();
    if (StatePill("Velocity", state->velocity, fonts, design, dpi, pad))
        engine.Send({ShellEngine::Action::Velocity, {}, 0, 0, !state->velocity});
    ImGui::SameLine();
    const bool sustainEnabled = !state->playing && !state->liveActive;
    if (StatePill("Sustain", state->sustain, fonts, design, dpi, pad, sustainEnabled,
                  sustainEnabled ? nullptr : "Stop playback and live input before changing sustain."))
        engine.Send({ShellEngine::Action::Sustain, {}, 0, 0, !state->sustain});
    ImGui::SameLine();
    StatePill("88 Keys", true, fonts, design, dpi, pad, false, "The shell uses the 88-key mapping.");
    ImGui::SameLine();
    StatePill("MidiConnect", false, fonts, design, dpi, pad, false, "Unavailable in this shell.");
}

void DevicePill(const std::string& name, float maxWidth, const skin::Skin& s, float dpi) {
    const auto min = ImGui::GetCursorScreenPos();
    const float width = std::min(maxWidth, ImGui::CalcTextSize(name.c_str()).x + 26 * dpi);
    skin::RaisedRect(ImGui::GetWindowDrawList(), min, ImVec2(min.x + width, min.y + s.metric.controlHeight),
                     s.radius.control, s, Colour(s.surface.card));
    DrawEllipsis(name, width - 24 * dpi, ImVec2(min.x + 12 * dpi,
        min.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    ImGui::Dummy(ImVec2(width, s.metric.controlHeight));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", name.c_str());
}

bool SettingSwitch(const char* label, bool& value, const char* description,
                   const Fonts& fonts, const skin::Skin& design, float dpi) {
    const auto s = skin::ScaleGeometry(design, dpi);
    const auto min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    const bool clicked = ImGui::Button("##switch", ImVec2(width, s.metric.controlHeight));
    ImGui::PopStyleColor(2);
    if (clicked) value = !value;
    auto* draw = ImGui::GetWindowDrawList();
    DrawEllipsis(label, width - 44 * dpi, ImVec2(min.x, min.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    const ImVec2 rail(min.x + width - 32 * dpi, min.y + (s.metric.controlHeight - 16 * dpi) / 2);
    draw->AddRectFilled(rail, ImVec2(rail.x + 32 * dpi, rail.y + 16 * dpi), Colour(value ? s.accent.okSoft : s.surface.recessed), 8 * dpi);
    draw->AddRect(rail, ImVec2(rail.x + 32 * dpi, rail.y + 16 * dpi), Colour(value ? s.accent.okBorder : s.border.strong), 8 * dpi, 0, dpi);
    draw->AddCircleFilled(ImVec2(rail.x + (value ? 24 : 8) * dpi, rail.y + 8 * dpi), 5 * dpi, Colour(value ? s.accent.okInk : s.ink.secondary));
    ImGui::PopID();
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
      ImGui::PushStyleColor(ImGuiCol_Text, Colour(s.ink.secondary));
      ImGui::TextWrapped("%s", description);
      ImGui::PopStyleColor(); }
    return clicked;
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
        preferences.alwaysOnTop = json.value("alwaysOnTop", false);
        const auto folder = json.value("midiFolder", std::string());
        preferences.folder = std::filesystem::path(std::u8string(folder.begin(), folder.end()));
    } catch (const std::exception&) { preferences = {}; }
}

void Panels::SavePreferences(const std::filesystem::path& path) const {
    nlohmann::json json{{"skin", preferences.skin}, {"autoSoloPiano", preferences.autoSolo},
                        {"midiFolder", Utf8(preferences.folder)}, {"keyMappingOpen", preferences.keyMappingOpen},
                        {"alwaysOnTop", preferences.alwaysOnTop}};
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
            const auto ink = key.black ? IM_COL32(221, 177, 106, 255) : IM_COL32(51, 47, 41, 255);
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
          "Layout 88-key     Range " + NoteName(whites.front()) + "\xe2\x80\x93" + NoteName(whites.back());
      Ellipsis(status, width - 2 * pad); }
    ImGui::PopFont();
    ImGui::GetStyle() = previousStyle;
    ImGui::End();
}

Panels::~Panels() { if (measuring_) input_latency::stop(); }

ImVec2 Panels::DesiredSize() const {
    const bool modern = preferences.skin >= 2;
    if (miniMode) return ImVec2(480, miniAutoplay ? (modern ? 305.f : 264.f) : (modern ? 243.f : 166.f));
    return ImVec2(1090, velocityExpanded ? (modern ? 1115.f : 1009.f) : (modern ? 728.f : 635.f));
}

namespace {
const char* BackendName(MidiBackend backend) {
    switch (backend) {
    case MidiBackend::WinMM: return "WinMM";
    case MidiBackend::WootingAnalog: return "Wooting Analog";
    default: return "WinRT";
    }
}
std::string DeviceName(const EngineSnapshot& state) {
    if (state.liveDevice.empty()) return "No MIDI input";
    for (const auto& device : state.devices) if (device.id == state.liveDevice) return device.name;
    return "Connected MIDI input";
}
void CurveCombo(const char* id, float width, const EngineSnapshot& state, ShellEngine& engine) {
    ImGui::SetNextItemWidth(width);
    const auto& edit = state.comparingCurve ? state.previousCurve : state.curve;
    const bool curveOpen = ImGui::BeginCombo(id, state.ActiveVelocityName().c_str(), ImGuiComboFlags_NoArrowButton);
    ComboChevron();
    if (curveOpen) {
        for (size_t i = 0; i < state.curves.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(state.curves[i].name.c_str(), i == edit.preset))
                engine.Send({ShellEngine::Action::CurveSelect, {}, 0, i});
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}
void DrawCurveLine(ImDrawList* draw, const VelocityPreset& preset, const VelocityEdit& edit,
                   ImVec2 min, ImVec2 max, ImU32 color, float thickness) {
    for (int i = 0; i <= 96; ++i) {
        const float x = i / 96.f;
        draw->PathLineTo(ImVec2(min.x + x * (max.x - min.x), max.y - VelocityShape(preset, edit, x) * (max.y - min.y)));
    }
    draw->PathStroke(color, 0, thickness);
}
}

void Panels::DrawVelocity(const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine,
                          ImVec2 min, ImVec2 max) {
    const auto state = engine.Snapshot();
    const auto s = skin::ScaleGeometry(design, dpi);
    BeginPanel("Velocity", min, max, s);
    ImGui::PushFont(fonts.Get(design), design.type.body * SpecFontScale(design));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    const auto start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x, control = s.metric.controlHeight;
    const bool expanded = velocityExpanded;
    if (TransportButton("##curve-disclosure", expanded ? Icon::Down : Icon::Right, "Velocity Response", s, dpi))
        velocityExpanded = !velocityExpanded;
    if (!expanded) {
        ImGui::SameLine();
        CurveCombo("##collapsed-curve", std::max(100 * dpi, width - 430 * dpi), *state, engine);
    }
    const float cutoffX = start.x + width - 248 * dpi;
    ImGui::SetCursorScreenPos(ImVec2(cutoffX, start.y));
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design)); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Sustain cutoff"); }
    ImGui::SameLine();
    // Commit on release, just like the macro sliders.
    float cutoff = cutoffEditing_ ? cutoffPreview_ : static_cast<float>(state->sustainCutoff);
    if (Groove("##sustain-cutoff", &cutoff, 0, 127, 120 * dpi, control, s, dpi, true)) {
        if (!ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
            engine.Send({ShellEngine::Action::SustainCutoff, {}, 0, 0, false, std::round(cutoff)});
        cutoffPreview_ = cutoff; cutoffEditing_ = true;
    }
    if (cutoffEditing_ && ImGui::IsItemDeactivatedAfterEdit()) {
        engine.Send({ShellEngine::Action::SustainCutoff, {}, 0, 0, false, std::round(cutoffPreview_)});
        cutoffEditing_ = false;
    }
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::Text("%.0f", cutoff);
    if (!expanded || state->curves.empty()) { ImGui::PopStyleVar(); ImGui::PopFont(); ImGui::EndChild(); return; }
    if (editorRevision_ != state->curveRevision || (!state->error.empty() && state->error != editorError_)) {
        editor_ = state->curve; editorRevision_ = state->curveRevision;
        cutoffEditing_ = false;
    }
    editorError_ = state->error;
    const auto openName = [&](int operation) {
        nameOperation_ = operation; focusCurveName_ = true; nameRevision_ = state->curveRevision;
        auto name = operation == 1 ? "New Curve" : state->curves[state->curve.preset].name;
        if (operation == 2 || (operation == 3 && state->curve.preset < 5)) name += " Copy";
        snprintf(curveName_, sizeof(curveName_), "%s", name.c_str());
    };
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + control + 12 * dpi));
    Ellipsis(state->ActiveVelocityName(), width - 3 * control - 24 * dpi);
    ImGui::SetCursorScreenPos(ImVec2(start.x + width - 3 * control - 16 * dpi, start.y + control + 12 * dpi));
    if (IconButton("##duplicate-curve", Icon::Copy, "Duplicate curve", s, dpi)) openName(2);
    ImGui::SameLine();
    if (IconButton("##rename-curve", Icon::Rename, "Rename curve", s, dpi)) openName(3);
    ImGui::SameLine();
    if (IconButton("##new-curve", Icon::Plus, "New curve", s, dpi)) openName(1);
    float workspaceY = start.y + 2 * control + 24 * dpi;
    if (nameOperation_) {
        ImGui::SetCursorScreenPos(ImVec2(start.x, workspaceY));
        ImGui::SetNextItemWidth(width - 2 * control - 16 * dpi);
        if (focusCurveName_) { ImGui::SetKeyboardFocusHere(); focusCurveName_ = false; }
        const bool enter = ImGui::InputTextWithHint("##curve-name", "Curve name", curveName_, sizeof(curveName_),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool cancel = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        ImGui::SameLine();
        const bool save = IconButton("##save-curve-name", Icon::Check, "Save curve name", s, dpi);
        ImGui::SameLine();
        if (IconButton("##cancel-curve-name", Icon::Close, "Cancel curve name", s, dpi) || cancel) nameOperation_ = 0;
        if ((enter || save) && nameOperation_ && curveName_[0] && nameRevision_ == state->curveRevision) {
            const auto action = nameOperation_ == 1 ? ShellEngine::Action::CurveNew :
                nameOperation_ == 2 ? ShellEngine::Action::CurveDuplicate : ShellEngine::Action::CurveRename;
            engine.Send({action, {}, 0, 0, false, 0, curveName_}); nameOperation_ = 0;
        }
        workspaceY += control + 12 * dpi;
    }
    const float presetsWidth = std::min(236 * dpi, width * .36f), mainWidth = width - presetsWidth - 12 * dpi;
    const float graphHeight = (design.type.family == "IBM Plex Sans" ? 208.f : 192.f) * dpi;
    const auto& shown = state->comparingCurve ? state->previousCurve : editor_;
    const auto& preset = state->comparingCurve ? state->previousPreset : state->curves[shown.preset];
    const ImVec2 graphMin(start.x, workspaceY), graphMax(start.x + mainWidth, workspaceY + graphHeight);
    auto* draw = ImGui::GetWindowDrawList();
    skin::RecessedRect(draw, graphMin, graphMax, s.radius.element, s);
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
      draw->AddText(ImVec2(graphMin.x + 12 * dpi, graphMin.y + 8 * dpi), Colour(s.ink.secondary), "32 game steps"); }
    const ImVec2 plotMin(graphMin.x + 12 * dpi, graphMin.y + 40 * dpi), plotMax(graphMax.x - 12 * dpi, graphMax.y - 28 * dpi);
    if (histogramRevision_ != state->playedVelocities.revision) {
        histogramRevision_ = state->playedVelocities.revision;
        const auto largest = *std::max_element(state->playedVelocities.buckets.begin(),
                                               state->playedVelocities.buckets.end());
        histogramVisible_ = state->playedVelocities.total != 0 && largest != 0;
        for (size_t i = 0; i < histogramHeights_.size(); ++i)
            histogramHeights_[i] = largest ? static_cast<float>(state->playedVelocities.buckets[i]) / largest : 0.f;
    }
    if (histogramVisible_) {
        const float barWidth = (plotMax.x - plotMin.x) / histogramHeights_.size();
        for (size_t i = 0; i < histogramHeights_.size(); ++i) {
            const float height = histogramHeights_[i] * (plotMax.y - plotMin.y) * .32f;
            if (height <= 0) continue;
            draw->AddRectFilled(ImVec2(plotMin.x + i * barWidth + dpi, plotMax.y - height),
                                ImVec2(plotMin.x + (i + 1) * barWidth - dpi, plotMax.y),
                                Colour(s.accent.accentSoft), 1.5f * dpi);
        }
    }
    for (int i = 1; i < 4; ++i) {
        const float x = plotMin.x + (plotMax.x - plotMin.x) * i / 4;
        const float y = plotMin.y + (plotMax.y - plotMin.y) * i / 4;
        draw->AddLine(ImVec2(x, plotMin.y), ImVec2(x, plotMax.y), Colour(s.border.hairline), dpi);
        draw->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), Colour(s.border.hairline), dpi);
    }
    for (int i = 0; i < 32; i += 2) {
        const auto point = [&](float t) { return ImVec2(plotMin.x + t * (plotMax.x - plotMin.x), plotMax.y - t * (plotMax.y - plotMin.y)); };
        draw->AddLine(point(i / 32.f), point((i + 1) / 32.f), Colour(s.ink.tertiary), dpi);
    }
    const auto thresholds = VelocityThresholds(preset, shown);
    for (int input = 1; input <= 127; ++input) {
        const float y = plotMax.y - VelocityBucket(thresholds, input) / 31.f * (plotMax.y - plotMin.y);
        const float x = plotMin.x + (input - 1) / 127.f * (plotMax.x - plotMin.x);
        draw->PathLineTo(ImVec2(x, y));
        draw->PathLineTo(ImVec2(plotMin.x + input / 127.f * (plotMax.x - plotMin.x), y));
    }
    draw->PathStroke(Colour(s.ink.tertiary), 0, dpi);
    DrawCurveLine(draw, preset, shown, plotMin, plotMax, Colour(s.accent.accent), 2 * dpi);
    ImGui::SetCursorScreenPos(plotMin);
    ImGui::InvisibleButton("##curve-graph", ImVec2(plotMax.x - plotMin.x, plotMax.y - plotMin.y));
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
      if (state->playedVelocities.last != 0) {
          const int input = state->playedVelocities.last;
          const int output = VelocityBucket(thresholds, input);
          char readout[64]; snprintf(readout, sizeof(readout), "%d played > step %d", input, output + 1);
          draw->AddText(ImVec2(graphMax.x - 12 * dpi - ImGui::CalcTextSize(readout).x, graphMin.y + 8 * dpi), Colour(s.ink.primary), readout);
          const float response = VelocityShape(preset, shown, input / 127.f);
          const ImVec2 dot(plotMin.x + input / 127.f * (plotMax.x - plotMin.x),
                           plotMax.y - response * (plotMax.y - plotMin.y));
          draw->AddCircleFilled(dot, 7 * dpi, Colour(s.surface.card));
          draw->AddCircle(dot, 7 * dpi, Colour(s.accent.accent), 20, 2 * dpi);
          draw->AddCircleFilled(dot, 2.7f * dpi, Colour(s.accent.accent));
      }
      draw->AddText(ImVec2(plotMin.x, graphMax.y - 20 * dpi), Colour(s.ink.tertiary), "Gentle");
      const char* label = "How hard you play";
      draw->AddText(ImVec2(graphMin.x + (mainWidth - ImGui::CalcTextSize(label).x) / 2, graphMax.y - 20 * dpi), Colour(s.ink.secondary), label);
      draw->AddText(ImVec2(plotMax.x - ImGui::CalcTextSize("Firm").x, graphMax.y - 20 * dpi), Colour(s.ink.tertiary), "Firm"); }
    const float macroY = graphMax.y + 12 * dpi, macroWidth = (mainWidth - 12 * dpi) / 2;
    ImGui::BeginDisabled(state->comparingCurve);
    for (int i = 0; i < 2; ++i) {
        const float x = start.x + i * (macroWidth + 12 * dpi);
        skin::RecessedRect(draw, ImVec2(x, macroY), ImVec2(x + macroWidth, macroY + 80 * dpi), s.radius.element, s);
        const char* label = i ? "Contrast" : "Sensitivity";
        draw->AddText(ImVec2(x + 12 * dpi, macroY + 8 * dpi), Colour(s.ink.primary), label);
        const float value = i ? editor_.contrast : editor_.sensitivity;
        const char* description = i ? (value >= 72 ? "Dramatic" : value >= 42 ? "Clear dynamics" : value >= 16 ? "Gentle contrast" : "Even response") :
            (value >= 15 ? "Light touch" : value >= 5 ? "Slightly lighter" : value <= -15 ? "Firm touch" : value <= -5 ? "Slightly firmer" : "Neutral");
        { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
          draw->AddText(ImVec2(x + 12 * dpi, macroY + 30 * dpi), Colour(s.ink.secondary), description); }
        ImGui::SetCursorScreenPos(ImVec2(x + 12 * dpi, macroY + 48 * dpi));
        float* target = i ? &editor_.contrast : &editor_.sensitivity;
        const bool changed = Groove(i ? "##contrast" : "##sensitivity", target, i ? 0.f : -50.f, i ? 100.f : 50.f,
            macroWidth - 24 * dpi, 24 * dpi, s, dpi, true);
        if (changed) editor_.manual = false;
        if (ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive()))
            engine.Send({ShellEngine::Action::CurveAdjust, {}, 0, 0, false, *target, i ? "contrast" : "sensitivity"});
    }
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(ImVec2(start.x, macroY + 92 * dpi));
    ImGui::BeginDisabled(!state->hasPreviousCurve);
    if (ImGui::Button(state->comparingCurve ? "A/B: Back to edit" : "A/B: Hear previous", ImVec2(0, control)))
        engine.Send({ShellEngine::Action::CurveCompare});
    ImGui::EndDisabled(); ImGui::SameLine();
    if (TransportButton("##advanced-curve", advancedCurve_ ? Icon::Down : Icon::Right, "Advanced", s, dpi)) advancedCurve_ = !advancedCurve_;
    float mainBottom = macroY + 92 * dpi + control;
    if (advancedCurve_) {
        const float y = mainBottom + 12 * dpi;
        ImGui::SetCursorScreenPos(ImVec2(start.x, y));
        { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design)); ImGui::TextUnformatted("32 game steps"); }
        const ImVec2 stepMin(start.x, y + 24 * dpi), stepMax(start.x + mainWidth, y + 96 * dpi);
        skin::RecessedRect(draw, stepMin, stepMax, s.radius.element, s);
        std::array<float, 32> values{};
        for (int i = 0; i < 32; ++i) values[i] = VelocityShape(preset, shown, i / 31.f);
        ImGui::SetCursorScreenPos(stepMin);
        ImGui::BeginDisabled(state->comparingCurve);
        ImGui::InvisibleButton("##step-editor", ImVec2(mainWidth, 72 * dpi));
        if (ImGui::IsItemActive()) {
            if (!editor_.manual) { editor_.samples = values; editor_.manual = true; }
            editingStep_ = std::clamp(static_cast<int>((ImGui::GetIO().MousePos.x - stepMin.x) / mainWidth * 32), 0, 31);
            stepValue_ = std::clamp((stepMax.y - ImGui::GetIO().MousePos.y) / (72 * dpi),
                editingStep_ ? values[editingStep_ - 1] : 0.f, editingStep_ < 31 ? values[editingStep_ + 1] : 1.f);
            values[editingStep_] = stepValue_;
            editor_.samples[editingStep_] = stepValue_;
        }
        if (editingStep_ >= 0 && ImGui::IsItemDeactivated()) {
            ShellEngine::Command command{ShellEngine::Action::CurveSteps}; command.samples = editor_.samples;
            engine.Send(std::move(command)); editingStep_ = -1;
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            const int step = std::clamp(static_cast<int>((ImGui::GetIO().MousePos.x - stepMin.x) / mainWidth * 32), 0, 31);
            ImGui::SetTooltip("Input %d: output step %.1f", static_cast<int>(std::round(step * 127.f / 31)), values[step] * 31 + 1);
        }
        ImGui::EndDisabled();
        for (int i = 0; i < 32; ++i) {
            const float x = stepMin.x + i * mainWidth / 32;
            draw->AddRectFilled(ImVec2(x + dpi, stepMax.y - values[i] * 68 * dpi - 2 * dpi),
                ImVec2(x + mainWidth / 32 - dpi, stepMax.y - 2 * dpi), Colour(s.accent.accent), dpi);
        }
        mainBottom = stepMax.y;
    }
    const ImVec2 listMin(start.x + mainWidth + 12 * dpi, workspaceY);
    skin::RecessedRect(draw, listMin, ImVec2(start.x + width, mainBottom), s.radius.element, s);
    ImGui::SetCursorScreenPos(ImVec2(listMin.x + 8 * dpi, listMin.y + 8 * dpi));
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design)); ImGui::TextUnformatted("Starting points"); }
    ImGui::SetCursorScreenPos(ImVec2(listMin.x + 4 * dpi, listMin.y + 32 * dpi));
    ImGui::BeginChild("##curve-list", ImVec2(presetsWidth - 8 * dpi, mainBottom - listMin.y - 36 * dpi), 0, ImGuiWindowFlags_NoBackground);
    for (size_t i = 0; i < state->curves.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const auto row = ImGui::GetCursorScreenPos();
        const float rowWidth = ImGui::GetContentRegionAvail().x, rowHeight = 40 * dpi;
        const bool selected = i == shown.preset;
        if (ImGui::Selectable("##preset", selected, 0, ImVec2(rowWidth, rowHeight))) {
            engine.Send({ShellEngine::Action::CurveSelect, {}, 0, i}); nameOperation_ = 0;
        }
        auto* listDraw = ImGui::GetWindowDrawList();
        VelocityEdit plain; plain.preset = i;
        DrawCurveLine(listDraw, state->curves[i], plain, ImVec2(row.x + 4 * dpi, row.y + 8 * dpi),
            ImVec2(row.x + 40 * dpi, row.y + 32 * dpi), Colour(selected ? s.accent.accent : s.ink.tertiary), 1.5f * dpi);
        DrawEllipsis(state->curves[i].name, rowWidth - 64 * dpi, ImVec2(row.x + 48 * dpi, row.y + (rowHeight - ImGui::GetTextLineHeight()) / 2));
        if (selected) DrawIcon(listDraw, Icon::Check, ImVec2(row.x + rowWidth - 16 * dpi, row.y + 12 * dpi), 14 * dpi, Colour(s.ink.primary), dpi);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s", state->curves[i].name.c_str(), i >= 5 ? " (custom)" : "");
        if (selected && listRevision_ != state->curveRevision) ImGui::SetScrollHereY(.5f);
        ImGui::PopID();
    }
    listRevision_ = state->curveRevision;
    ImGui::EndChild();
    ImGui::SetCursorScreenPos(ImVec2(start.x, mainBottom + 4 * dpi)); ImGui::Dummy(ImVec2(width, 1));
    ImGui::PopStyleVar(); ImGui::PopFont();
    ImGui::EndChild();
}

void Panels::DrawSettings(const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    const auto state = engine.Snapshot();
    const auto s = skin::ScaleGeometry(design, dpi);
    FontScope font(fonts, design, design.type.body * SpecFontScale(design));
    const auto section = [&](const char* label) {
        FontScope meta(fonts, design, design.type.meta * SpecFontScale(design), Weight::Semibold);
        ImGui::PushStyleColor(ImGuiCol_Text, Colour(s.ink.secondary));
        ImGui::TextUnformatted(label); ImGui::PopStyleColor();
    };
    section("MIDI INPUT");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    // The list and every open command use the ids supplied by ShellEngine.
    for (const auto backend : {MidiBackend::WinRT, MidiBackend::WinMM, MidiBackend::WootingAnalog}) {
        const auto available = std::find_if(state->devices.begin(), state->devices.end(),
            [&](const LiveDevice& device) { return BackendForDeviceId(device.id) == backend; });
        ImGui::BeginDisabled(available == state->devices.end());
        const bool selected = !state->liveDevice.empty() && BackendForDeviceId(state->liveDevice) == backend;
        if (ImGui::RadioButton(BackendName(backend), selected) && !selected) {
            ShellEngine::Command command{ShellEngine::Action::LiveOpen}; command.device = available->id; engine.Send(std::move(command));
        }
        ImGui::EndDisabled();
    }
    ImGui::TextDisabled("Kernel Streaming: unavailable");
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
      ImGui::TextWrapped("The device scan prefers WinRT, with WinMM as fallback. Kernel Streaming has no backend or buffer controls."); }
    ImGui::Separator();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - s.metric.controlHeight - 8 * dpi);
    const bool deviceOpen = ImGui::BeginCombo("##midi-input", DeviceName(*state).c_str(), ImGuiComboFlags_NoArrowButton);
    ComboChevron();
    if (deviceOpen) {
        if (ImGui::Selectable("No MIDI input", state->liveDevice.empty())) engine.Send({ShellEngine::Action::LiveOpen});
        for (size_t i = 0; i < state->devices.size(); ++i) {
            const auto& device = state->devices[i];
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(device.name.c_str(), state->liveDevice == device.id)) {
                ShellEngine::Command command{ShellEngine::Action::LiveOpen}; command.device = device.id; engine.Send(std::move(command));
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (IconButton("##scan-midi", Icon::Refresh, "Scan MIDI inputs", s, dpi)) engine.Send({ShellEngine::Action::LiveScan});
    const bool wootingSelected = state->liveDevice == L"wooting:analog" &&
        BackendForDeviceId(state->liveDevice) == MidiBackend::WootingAnalog;
    if (wootingSelected) {
        ImGui::Separator();
        section("WOOTING ANALOG");
        const std::array<float, 3> current{
            static_cast<float>(state->wootingTriggerThreshold),
            static_cast<float>(state->wootingShiftAmount),
            static_cast<float>(state->wootingVelocityScale)};
        const auto closeEnough = [](float a, float b) { return std::abs(a - b) < .001f; };
        for (size_t i = 0; i < current.size(); ++i) {
            if (wootingPending_[i] && closeEnough(current[i], wootingPreview_[i])) wootingPending_[i] = false;
            if (!wootingEditing_[i] && !wootingPending_[i]) wootingPreview_[i] = current[i];
        }
        const auto setting = [&](size_t index, const char* label, const char* id, float low, float high, float step,
                                 ShellEngine::Action action, const char* format) {
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            const float rounded = std::clamp(std::round(wootingPreview_[index] / step) * step, low, high);
            char text[32]; snprintf(text, sizeof(text), format, rounded);
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(text).x));
            ImGui::TextUnformatted(text);
            float value = wootingPreview_[index];
            const bool changed = Groove(id, &value, low, high, ImGui::GetContentRegionAvail().x,
                                        22 * dpi, s, dpi, true);
            value = std::clamp(std::round(value / step) * step, low, high);
            if (changed) {
                wootingPreview_[index] = value;
                ShellEngine::Command command{action}; command.amount = value;
                command.value = !ImGui::IsItemActive();
                engine.Send(std::move(command));
                wootingEditing_[index] = ImGui::IsItemActive();
                wootingPending_[index] = !wootingEditing_[index];
            }
            if (wootingEditing_[index] && ImGui::IsItemDeactivatedAfterEdit()) {
                ShellEngine::Command command{action}; command.amount = wootingPreview_[index]; command.value = true;
                engine.Send(std::move(command));
                wootingEditing_[index] = false;
                wootingPending_[index] = true;
            }
        };
        setting(0, "Note trigger threshold", "##wooting-trigger", .01f, 1.f, .01f,
                ShellEngine::Action::WootingTriggerThreshold, "%.2f");
        setting(1, "Shift amount", "##wooting-shift", -127.f, 127.f, 1.f,
                ShellEngine::Action::WootingShiftAmount, "%+.0f semitones");
        { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
          ImGui::TextWrapped("Set this to 1 and hold Left Shift to play a black key."); }
        setting(2, "Velocity scale", "##wooting-velocity", .1f, 20.f, .1f,
                ShellEngine::Action::WootingVelocityScale, "%.1f");
        ImGui::Separator();
    } else {
        wootingEditing_.fill(false);
        wootingPending_.fill(false);
    }
    bool active = state->liveActive;
    ImGui::BeginDisabled(state->liveDevice.empty());
    if (ImGui::Checkbox("Midi2Key", &active)) engine.Send({ShellEngine::Action::LiveActive, {}, 0, 0, active});
    ImGui::EndDisabled();
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
      ImGui::TextWrapped("Types incoming MIDI notes using the current key mapping."); }
    ImGui::SetNextItemWidth(-1);
    const auto channel = state->liveChannel < 0 ? "Every channel" : "Channel " + std::to_string(state->liveChannel + 1);
    const bool channelOpen = ImGui::BeginCombo("##live-channel", channel.c_str(), ImGuiComboFlags_NoArrowButton);
    ComboChevron();
    if (channelOpen) {
        for (int i = -1; i < 16; ++i) {
            const auto label = i < 0 ? "Every channel" : "Channel " + std::to_string(i + 1);
            if (ImGui::Selectable(label.c_str(), state->liveChannel == i))
                engine.Send({ShellEngine::Action::LiveChannel, {}, 0, 0, false, static_cast<double>(i)});
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();
    bool measure = measuring_;
    if (ImGui::Checkbox("Measure keyboard timing", &measure)) {
        if (measure) { timing_ = input_latency::Collector{}; timingSummary_ = {}; measuring_ = input_latency::start(); }
        else { input_latency::stop(); measuring_ = false; timingSummary_ = {}; }
    }
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
      ImGui::TextWrapped("Collects note observations while Settings is open."); }
    if (!measuring_ && input_latency::hookError()) ImGui::Text("Hook unavailable: %lu", input_latency::hookError());
    ImGui::SetNextItemWidth(-1);
    const char* sourceLabels[]{"Live input", "Autoplay"};
    if (ImGui::Combo("##timing-source", &timingSource_, sourceLabels, 2)) { timingSummary_ = {}; nextTimingPoll_ = 0; }
    if (measuring_) {
        const auto& t = timingSummary_;
        if (t.callbackToHookMs.count) {
            ImGui::Text("Callback to hook: %.3f ms median", t.callbackToHookMs.p50);
            ImGui::Text("p95 %.3f ms   p99 %.3f ms", t.callbackToHookMs.p95, t.callbackToHookMs.p99);
            ImGui::Text("Preparation %.3f ms   Calls %.3f ms", t.preparationMs.p50, t.callsMs.p50);
            ImGui::Text("%zu notes   %.2f events/note", t.notes, t.eventsPerNote);
            const auto graph = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            auto* draw = ImGui::GetWindowDrawList();
            skin::RecessedRect(draw, graph, ImVec2(graph.x + width, graph.y + 8 * dpi), 4 * dpi, s);
            const float observed = static_cast<float>(t.callbackToHookMs.count) / std::max(size_t{1}, t.notes);
            if (observed > 0) draw->AddRectFilled(graph, ImVec2(graph.x + width * observed, graph.y + 8 * dpi), Colour(s.accent.okInk), 4 * dpi);
            ImGui::Dummy(ImVec2(width, 8 * dpi));
            ImGui::Text("%zu of %zu notes fully observed", t.callbackToHookMs.count, t.notes);
        } else ImGui::TextUnformatted("Waiting for note observations");
        ImGui::Text("%zu incomplete   %llu failures   %llu dropped", t.incomplete,
            static_cast<unsigned long long>(t.failures), static_cast<unsigned long long>(input_latency::dropped()));
    }
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
      ImGui::TextWrapped("Starts after the MIDI transport; ends at the keyboard hook, before the game. Autoplay starts at event dispatch."); }
    ImGui::Separator();
    section("BEHAVIOUR");
    SettingSwitch("Solo piano tracks on load", preferences.autoSolo,
        "Silences non-piano parts when a MIDI file opens.", fonts, design, dpi);
    bool velocity = state->velocity;
    if (SettingSwitch("Velocity hotkeys", velocity,
        "Off skips the ALT velocity preamble for live input and autoplay.", fonts, design, dpi))
        engine.Send({ShellEngine::Action::Velocity, {}, 0, 0, velocity});
    SettingSwitch("Always on top", preferences.alwaysOnTop,
        "Keeps the main window above other windows.", fonts, design, dpi);
    ImGui::Separator();
    section("APPEARANCE");
    if (ImGui::RadioButton("Classic", preferences.skin < 2)) preferences.skin %= 2;
    ImGui::SameLine();
    if (ImGui::RadioButton("Modern", preferences.skin >= 2)) preferences.skin = 2 + preferences.skin % 2;
    if (ImGui::CollapsingHeader("About")) {
        ImGui::TextWrapped("Based on Zephkek/MIDIPlusPlus (GPLv3)");
        ImGui::TextWrapped("Dear ImGui and RtMidi (MIT)");
        ImGui::TextWrapped("IBM Plex Sans (SIL Open Font License 1.1)");
        ImGui::TextWrapped("Segoe UI and CJK fallback fonts from Windows");
    }
    ImGui::PopStyleVar();
}

void Panels::DrawStatus(const Fonts& fonts, const skin::Skin& design, float dpi, const EngineSnapshot& state,
                       ImVec2 min, float width, float height) {
    const auto s = skin::ScaleGeometry(design, dpi);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, ImVec2(min.x + width, min.y + height), Colour(s.surface.structure));
    draw->AddLine(min, ImVec2(min.x + width, min.y), Colour(s.border.hairline), dpi);
    FontScope font(fonts, design, design.type.meta * SpecFontScale(design));
    const ImVec2 text(min.x + s.spacing.windowPad, min.y + (height - ImGui::GetTextLineHeight()) / 2);
    std::vector<std::string> fields;
    if (!state.error.empty()) fields.push_back(state.error);
    else if (state.busy) fields.push_back("Loading...");
    else {
        fields.push_back(state.playing ? "Playing" : state.liveActive ? "Live" : "Ready");
        fields.push_back("Curve " + state.ActiveVelocityName());
        std::string input = "Input ";
        input += state.liveDevice.empty() ? "None" : BackendName(BackendForDeviceId(state.liveDevice));
        if (!state.liveDevice.empty() && !state.liveActive) input += " (off)";
        fields.push_back(input);
        if (!stopHotkeyAvailable) fields.push_back("Stop hotkey unavailable");
    }
    const auto tracks = std::to_string(SilentTracks(state.rows)) + " of " + std::to_string(state.rows.size()) + " tracks silent \xc2\xb7 88-key";
    const float suffix = miniMode ? 0 : ImGui::CalcTextSize(tracks.c_str()).x + 24 * dpi;
    const float end = min.x + width - s.spacing.windowPad - suffix;
    float x = text.x;
    std::string summary;
    for (const auto& field : fields) {
        if (!summary.empty()) summary += "\n";
        summary += field;
        if (x >= end) continue;
        if (x > text.x) {
            draw->AddLine(ImVec2(x + 12 * dpi, text.y + 2 * dpi),
                          ImVec2(x + 12 * dpi, text.y + ImGui::GetTextLineHeight() - 2 * dpi), Colour(s.border.hairline), dpi);
            x += 24 * dpi;
        }
        if (x < end) DrawEllipsis(field, end - x, ImVec2(x, text.y));
        x += ImGui::CalcTextSize(field.c_str()).x;
    }
    if (!miniMode) draw->AddText(ImVec2(min.x + width - s.spacing.windowPad - ImGui::CalcTextSize(tracks.c_str()).x, text.y), Colour(s.ink.secondary), tracks.c_str());
    ImGui::SetCursorScreenPos(text);
    ImGui::InvisibleButton("##status", ImVec2(width - 2 * s.spacing.windowPad, ImGui::GetTextLineHeight()));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", summary.c_str());
}

void Panels::DrawMini(HWND hwnd, const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine,
                     ImVec2 origin, ImVec2 size) {
    const auto state = engine.Snapshot();
    const auto s = skin::ScaleGeometry(design, dpi);
    FontScope font(fonts, design, design.type.body * SpecFontScale(design));
    const float control = s.metric.controlHeight, pad = s.spacing.windowPad;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (control - ImGui::GetTextLineHeight()) / 2));
    const float strip = control + 16 * dpi, status = 28 * dpi;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + strip), Colour(s.surface.structure));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + 8 * dpi));
    DevicePill(DeviceName(*state), std::max(40 * dpi, size.x - 280 * dpi), s, dpi);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 268 * dpi, origin.y + 8 * dpi));
    {
        FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
        const auto well = ImGui::GetCursorScreenPos();
        const float segmentWidth = 164 * dpi;
        skin::RecessedRect(draw, well, ImVec2(well.x + segmentWidth, well.y + control), s.radius.control, s);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s.radius.element);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        for (int mode = 0; mode < 2; ++mode) {
            const bool selected = miniAutoplay == (mode == 1);
            ImGui::SetCursorScreenPos(ImVec2(well.x + 4 * dpi + mode * 80 * dpi, well.y + 4 * dpi));
            ImGui::PushStyleColor(ImGuiCol_Button, selected ? Colour(s.surface.elevated) : IM_COL32(0, 0, 0, 0));
            if (ImGui::Button(mode ? "Autoplay" : "Live", ImVec2(76 * dpi, control - 8 * dpi))) miniAutoplay = mode == 1;
            ImGui::PopStyleColor();
            if (selected) {
                const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                draw->AddRect(a, b, Colour(s.border.strong), s.radius.element, 0, dpi);
            }
        }
        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos(ImVec2(well.x + segmentWidth + 8 * dpi, well.y));
    }
    if (IconButton("##mini-theme", s.dark ? Icon::Moon : Icon::Sun, "Switch theme", s, dpi)) preferences.skin ^= 1;
    ImGui::SameLine();
    if (IconButton("##restore-full", Icon::Expand, "Full window", s, dpi)) miniMode = false;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + strip + 8 * dpi));
    StatePills(fonts, design, dpi, engine, true);
    const float row = origin.y + strip + 16 * dpi + control;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, row));
    const auto number = [&](ShellEngine::Action action, double value) { engine.Send({action, {}, state->generation, 0, false, value}); };
    if (!miniAutoplay) {
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Curve"); ImGui::SameLine();
        CurveCombo("##mini-curve", size.x - 2 * pad - 292 * dpi, *state, engine);
        ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Transpose"); ImGui::SameLine();
        ImGui::BeginDisabled(state->transpose <= -12);
        if (IconButton("##mini-lower", Icon::Minus, "Transpose down", s, dpi)) number(ShellEngine::Action::Transpose, state->transpose - 1);
        ImGui::EndDisabled(); ImGui::SameLine();
        const auto value = ImGui::GetCursorScreenPos();
        const auto text = (state->transpose >= 0 ? "+" : "") + std::to_string(state->transpose);
        draw->AddText(ImVec2(value.x + (32 * dpi - ImGui::CalcTextSize(text.c_str()).x) / 2,
                            value.y + (control - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.primary), text.c_str());
        ImGui::Dummy(ImVec2(32 * dpi, control)); ImGui::SameLine();
        ImGui::BeginDisabled(state->transpose >= 12);
        if (IconButton("##mini-higher", Icon::Plus, "Transpose up", s, dpi)) number(ShellEngine::Action::Transpose, state->transpose + 1);
        ImGui::EndDisabled();
    } else {
        ImGui::SetNextItemWidth(size.x - 2 * pad - 2 * control - 16 * dpi);
        const auto fileName = state->loaded.empty() ? "Choose MIDI file" : Utf8(state->loaded.filename());
        const bool fileOpen = ImGui::BeginCombo("##mini-file", fileName.c_str(), ImGuiComboFlags_NoArrowButton);
        ComboChevron();
        if (fileOpen) {
            for (size_t i = 0; i < state->files->size(); ++i) {
                const auto& file = (*state->files)[i];
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(file.name.c_str(), file.path == state->loaded))
                    engine.Send({ShellEngine::Action::Load, file.path, 0, 0, preferences.autoSolo});
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (IconButton("##mini-open", Icon::Open, "Open MIDI file", s, dpi)) {
            const auto path = PickFile(hwnd);
            if (!path.empty()) engine.Send({ShellEngine::Action::Load, path, 0, 0, preferences.autoSolo});
        }
        ImGui::SameLine(); ImGui::BeginDisabled(state->rows.empty());
        if (IconButton("##mini-solo-piano", Icon::Piano, "Solo piano tracks", s, dpi)) engine.Send({ShellEngine::Action::SoloPiano, {}, state->generation});
        ImGui::EndDisabled();
        ImGui::BeginDisabled(state->loaded.empty() || state->busy);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, row + control + 8 * dpi));
        if (!seeking_ || seekGeneration_ != state->generation) { seekPosition_ = static_cast<float>(state->position); seeking_ = false; }
        const bool changed = Groove("##mini-seek", &seekPosition_, 0, static_cast<float>(std::max(.001, state->duration)),
            size.x - 2 * pad, 22 * dpi, s, dpi, false);
        if (ImGui::IsItemActivated()) { seeking_ = true; seekGeneration_ = state->generation; }
        if (seeking_ && ImGui::IsItemDeactivatedAfterEdit()) { number(ShellEngine::Action::Seek, seekPosition_); seeking_ = false; }
        else if (changed && !ImGui::IsItemActive()) number(ShellEngine::Action::Seek, seekPosition_);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, row + control + 38 * dpi));
        if (TransportButton("##mini-play", state->playing ? Icon::Pause : Icon::Play, state->playing ? "Pause" : "Play", s, dpi, true))
            engine.Send({ShellEngine::Action::TogglePlayPause, {}, state->generation});
        ImGui::SameLine();
        if (IconButton("##mini-restart", Icon::Refresh, "Restart", s, dpi)) engine.Send({ShellEngine::Action::Restart, {}, state->generation});
        ImGui::SameLine();
        if (IconButton("##mini-back10", Icon::Back, "Back 10 seconds", s, dpi)) engine.Send({ShellEngine::Action::Back10, {}, state->generation});
        ImGui::SameLine();
        if (IconButton("##mini-forward10", Icon::Forward, "Forward 10 seconds", s, dpi)) engine.Send({ShellEngine::Action::Forward10, {}, state->generation});
        ImGui::EndDisabled();
        const auto time = Time(seeking_ ? seekPosition_ : state->position) + " / " + Time(state->duration);
        draw->AddText(ImVec2(origin.x + size.x - pad - ImGui::CalcTextSize(time.c_str()).x,
            row + control + 38 * dpi + (control - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.secondary), time.c_str());
    }
    DrawStatus(fonts, design, dpi, *state, ImVec2(origin.x, origin.y + size.y - status), size.x, status);
    ImGui::PopStyleVar();
}

void Panels::Draw(HWND hwnd, const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    const auto s = skin::ScaleGeometry(design, dpi);
    auto state = engine.Snapshot();
    if (!scannedLive_ && hwnd) { engine.Send({ShellEngine::Action::LiveScan}); scannedLive_ = true; }
    if (measuring_ && ImGui::GetTime() >= nextTimingPoll_) {
        input_latency::poll(timing_);
        timingSummary_ = timing_.summarize(timingSource_ ? input_latency::Source::Autoplay : input_latency::Source::LiveKeys,
                                          input_latency::frequency());
        nextTimingPoll_ = ImGui::GetTime() + .2;
    }
    const auto send = [&](ShellEngine::Action action, size_t track = 0, bool value = false) {
        engine.Send({action, {}, state->generation, track, value});
    };
    const auto load = [&](const std::filesystem::path& path) {
        if (!path.empty()) engine.Send({ShellEngine::Action::Load, path, 0, 0, preferences.autoSolo});
    };

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (miniMode) { DrawMini(hwnd, fonts, design, dpi, engine, origin, size); mappingArmed_ = false; return; }
    auto* dl = ImGui::GetWindowDrawList();
    const bool modernStrip = design.type.family == "IBM Plex Sans";
    const float stripPad = (modernStrip ? 12.f : 8.f) * dpi;
    const float strip = (modernStrip ? 101.f : 81.f) * dpi, status = 28 * dpi;
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + strip), Colour(s.surface.structure));
    dl->AddLine(ImVec2(origin.x, origin.y + strip), ImVec2(origin.x + size.x, origin.y + strip), Colour(s.border.hairline));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + s.spacing.windowPad, origin.y + stripPad));
    { FontScope font(fonts, design, design.type.body * SpecFontScale(design), Weight::Medium);
      DevicePill(DeviceName(*state), size.x - 240 * dpi, s, dpi); }
    ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - s.spacing.windowPad - 4 * s.metric.controlHeight - 3 * s.spacing.s2,
                                    origin.y + stripPad));
    if (IconButton("##mini-mode", Icon::Mini, "Mini mode", s, dpi)) miniMode = true;
    ImGui::SameLine();
    if (IconButton("##key-mapping", Icon::Keyboard, "Key Mapping", s, dpi, preferences.keyMappingOpen))
        preferences.keyMappingOpen = !preferences.keyMappingOpen;
    ImGui::SameLine();
    if (IconButton("##theme", s.dark ? Icon::Moon : Icon::Sun, s.dark ? "Switch to light" : "Switch to dark", s, dpi))
        preferences.skin ^= 1;
    ImGui::SameLine();
    if (IconButton("##settings", Icon::Settings, "Settings", s, dpi)) ImGui::OpenPopup("Settings");
    ImGui::SetNextWindowSizeConstraints(ImVec2(344 * dpi, 0), ImVec2(344 * dpi, size.y - 52 * dpi));
    ImGui::SetNextWindowPos(ImVec2(origin.x + size.x - 344 * dpi - s.spacing.windowPad, origin.y + 48 * dpi));
    if (ImGui::BeginPopup("Settings")) {
        DrawSettings(fonts, design, dpi, engine);
        ImGui::EndPopup();
    } else if (measuring_) { input_latency::stop(); measuring_ = false; timingSummary_ = {}; }
    ImGui::SetCursorScreenPos(ImVec2(origin.x + s.spacing.windowPad, origin.y + strip - s.metric.controlHeight - stripPad - dpi));
    StatePills(fonts, design, dpi, engine, false);

    const float top = origin.y + strip + s.spacing.windowPad;
    const float bottom = origin.y + size.y - status - s.spacing.windowPad;
    const float leftWidth = std::min(336 * dpi, size.x * .33f);
    const ImVec2 leftMin(origin.x + s.spacing.windowPad, top);
    const ImVec2 leftMax(leftMin.x + leftWidth, bottom);
    BeginPanel("Files", leftMin, leftMax, s);
    ImGui::PushFont(fonts.Get(design), design.type.body * SpecFontScale(design));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    { FontScope font(fonts, design, design.type.body * SpecFontScale(design), Weight::Semibold);
      ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("MIDI Files"); }
    const float sortWidth = ImGui::CalcTextSize("Name").x + 24 * dpi + 18 * dpi + s.spacing.s2;
    const float refreshWidth = ImGui::CalcTextSize("Refresh").x + 16 * dpi;
    ImGui::SameLine(ImGui::GetWindowWidth() - sortWidth - refreshWidth - s.spacing.s2);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * dpi, ImGui::GetStyle().FramePadding.y));
    if (TransportButton("##sort-name", descendingNames_ ? Icon::SortUp : Icon::SortDown, "Name", s, dpi)) {
        descendingNames_ = !descendingNames_;
        filteredFiles_.reset();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", descendingNames_ ? "Name: Z to A" : "Name: A to Z");
    ImGui::SameLine();
    ImGui::BeginDisabled(state->playing || state->busy || state->folder.empty());
    if (ImGui::Button("Refresh", ImVec2(refreshWidth, s.metric.controlHeight))) engine.Send({ShellEngine::Action::Scan, state->folder});
    ImGui::EndDisabled(); ImGui::PopStyleVar();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2 * s.metric.controlHeight - 2 * s.spacing.s2);
    ImGui::InputTextWithHint("##search", "Search MIDI files", search_, sizeof(search_));
    ImGui::SameLine();
    if (IconButton("##open", Icon::Open, "Open MIDI file", s, dpi)) load(PickFile(hwnd));
    ImGui::SameLine();
    ImGui::BeginDisabled(state->playing || state->busy);
    if (IconButton("##folder", Icon::Folder, "Choose MIDI folder", s, dpi)) {
        const auto path = PickFolder(hwnd);
        if (!path.empty()) { preferences.folder = path; engine.Send({ShellEngine::Action::Scan, path}); }
    }
    ImGui::EndDisabled();
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
            std::stable_sort(fileFilter_.begin(), fileFilter_.end(), [&](size_t a, size_t b) {
                const auto first = lowercase((*state->files)[a].name), second = lowercase((*state->files)[b].name);
                return descendingNames_ ? first > second : first < second;
            });
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
            const bool selected = file.path == state->loaded;
            auto* listDraw = ImGui::GetWindowDrawList();
            if (selected) listDraw->AddRectFilled(pos, ImVec2(pos.x + 2 * dpi, pos.y + s.metric.controlHeight), Colour(s.accent.accent));
            FontScope rowFont(fonts, design, design.type.body * SpecFontScale(design), selected ? Weight::Semibold : Weight::Regular);
            const auto bytes = std::to_string((file.bytes + 1023) / 1024) + " KB";
            const float sizeWidth = ImGui::CalcTextSize(bytes.c_str()).x;
            const float textY = pos.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2;
            DrawEllipsis(file.name, width - sizeWidth - 3 * s.spacing.s3, ImVec2(pos.x + s.spacing.s3, textY));
            listDraw->AddText(ImVec2(pos.x + width - s.spacing.s3 - sizeWidth, textY), Colour(s.ink.secondary), bytes.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%llu bytes", file.name.c_str(), static_cast<unsigned long long>(file.bytes));
            ImGui::PopID();
        }
        if (fileFilter_.empty()) ImGui::TextDisabled("No matching files");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(); ImGui::PopFont();
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
    if (TransportButton("##back10", "−10s", s, dpi)) send(ShellEngine::Action::Back10);
    ImGui::SameLine();
    if (TransportButton("##forward10", "+10s", s, dpi)) send(ShellEngine::Action::Forward10);
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
    const float collapsedHeight = 2 * s.spacing.panelPad + s.metric.controlHeight;
    const float requestedCurveHeight = velocityExpanded ? ((modern ? 472.f : 444.f) +
        (advancedCurve_ ? 108.f : 0.f) + (nameOperation_ ? (modern ? 44.f : 40.f) : 0.f)) * dpi : collapsedHeight;
    const float curveHeight = std::min(requestedCurveHeight, std::max(collapsedHeight, bottom - trackTop - 168 * dpi));
    const float curveTop = bottom - curveHeight;
    BeginPanel("Tracks", ImVec2(right, trackTop), ImVec2(edge, curveTop - s.spacing.s3), s);
    ImGui::PushFont(fonts.Get(design), design.type.body * SpecFontScale(design));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    { FontScope font(fonts, design, design.type.body * SpecFontScale(design), Weight::Semibold); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Tracks"); }
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
        ImGui::TableSetupColumn("TRACK", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("INSTRUMENT", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("CH", ImGuiTableColumnFlags_WidthFixed, 28 * dpi);
        ImGui::TableSetupColumn("NOTES", ImGuiTableColumnFlags_WidthFixed, 48 * dpi);
        ImGui::TableSetupColumn("##mute-heading", ImGuiTableColumnFlags_WidthFixed, s.metric.controlHeight);
        ImGui::TableSetupColumn("##solo-heading", ImGuiTableColumnFlags_WidthFixed, s.metric.controlHeight);
        ImVec2 muteSoloMin, muteSoloMax;
        { FontScope font(fonts, design, design.type.meta * SpecFontScale(design), Weight::Semibold);
          ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
          for (int column = 0; column < 7; ++column) {
              ImGui::TableSetColumnIndex(column);
              // Always the column's own name, never "". An empty label takes
              // its ID from the parent, so the two icon columns collided with
              // each other and ImGui said so on screen.
              // The mute and solo names begin with ## and so draw nothing;
              // MUTE SOLO is painted across both of them below.
              ImGui::TableHeader(ImGui::TableGetColumnName(column));
          } }
        const auto* table = ImGui::GetCurrentTable();
        // The real header row, not a 24px guess offset by a spacing step: that
        // put MUTE SOLO on a different baseline from the five headers beside it.
        muteSoloMin = ImVec2(table->Columns[5].MinX, table->RowPosY1);
        muteSoloMax = ImVec2(table->Columns[6].MaxX, muteSoloMin.y + ImGui::TableGetHeaderRowHeight());
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
            { FontScope font(fonts, design, design.type.body * SpecFontScale(design), audible ? Weight::Medium : Weight::Regular);
              Ellipsis(row.name, ImGui::GetContentRegionAvail().x); }
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
            if (row.piano) {
                auto p = ImGui::GetCursorScreenPos();
                p.y += ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset + (ImGui::GetTextLineHeight() - 14 * dpi) / 2;
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
        { FontScope font(fonts, design, design.type.meta * SpecFontScale(design), Weight::Semibold);
          const float textWidth = ImGui::CalcTextSize("MUTE SOLO").x;
          auto* headers = ImGui::GetWindowDrawList();
          headers->PushClipRect(muteSoloMin, muteSoloMax, false);
          headers->AddText(ImVec2(muteSoloMin.x + (muteSoloMax.x - muteSoloMin.x - textWidth) / 2,
                                  muteSoloMin.y + (muteSoloMax.y - muteSoloMin.y - ImGui::GetTextLineHeight()) / 2),
                           Colour(s.ink.secondary), "MUTE SOLO");
          headers->PopClipRect(); }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleVar(); ImGui::PopFont();
    ImGui::EndChild();

    DrawVelocity(fonts, design, dpi, engine, ImVec2(right, curveTop), ImVec2(edge, bottom));
    DrawStatus(fonts, design, dpi, *state, ImVec2(origin.x, origin.y + size.y - status), size.x, status);
    if (preferences.keyMappingOpen) DrawKeyMapping(fonts, design, dpi, engine);
    else mappingArmed_ = false;
}
}
