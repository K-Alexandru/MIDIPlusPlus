#include "Panels.hpp"
#include "IconData.hpp"
#include "imgui_internal.h"
#include "json.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <fstream>
#include <cmath>
#include "MidiInput.hpp"
#include "MidiOutput.hpp"
#include "config.hpp"
#include "../MIDI++/AudioToMidi.hpp"

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
// The shipped IBM Plex hhea/head ratio is 1300/1000.
float SpecFontScale(const skin::Skin&) { return 1.3f; }
enum class Icon { Folder, Open, Refresh, Settings, Sun, Moon, Play, Pause, Back, Forward,
                  Minus, Plus, Left, Right, Down, Up, Close, Keyboard, Speaker, Muted, Solo, Piano,
                  Mini, Expand, Copy, Rename, Check, SortDown, SortUp, Undo, Redo, Anchor, Clear };

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
    // Shape as well as colour, per the house rules: an outline, not an
    // underline. The underline this replaces read as text decoration sitting
    // under an icon, and it was the only underline anywhere in the app. An
    // outline says "this one is on" with the same non-colour signal and matches
    // the selected segment of the mini Live/Autoplay control, which is the one
    // other place a toggle is marked by shape.
    if (active)
        ImGui::GetWindowDrawList()->AddRect(min, ImVec2(min.x + height, min.y + height),
                                            Colour(s.accent.accent), s.radius.control, 0, dpi);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tip);
    return clicked;
}

// Icon and label are centred as one unit. The width used to reserve 18px plus
// a gap for the icon while the label was drawn 24px in, which left every
// transport button two pixels wider on the right than on the left.
// `active` marks a toggle that is on, the same way IconButton does: the soft
// accent fill, accent ink and an outline.
// `reserve` lists every label the button can carry, so it keeps the widest
// one's width and the row beside it does not move when the label changes.
bool TransportBody(const char* id, const Icon* icon, const char* label,
                   const skin::Skin& s, float dpi, bool primary, bool active = false,
                   std::initializer_list<const char*> reserve = {}) {
    ImVec2 min = ImGui::GetCursorScreenPos();
    const float pad = (primary ? 16.f : 12.f) * dpi;
    const float side = 16 * dpi;
    const float lead = icon ? side + s.spacing.s2 : 0.f;
    const float labelWidth = ImGui::CalcTextSize(label).x;
    float reserved = labelWidth;
    for (const char* other : reserve) reserved = std::max(reserved, ImGui::CalcTextSize(other).x);
    const float width = 2 * pad + lead + reserved;
    const float frameX = min.x;
    min.x += std::floor((reserved - labelWidth) / 2);
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, Colour(s.accent.accentSoft));
    const bool clicked = ImGui::Button(id, ImVec2(width, s.metric.controlHeight));
    if (active) ImGui::PopStyleColor();
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 ink = active ? Colour(s.accent.accent) : ImGui::GetColorU32(ImGuiCol_Text);
    if (icon)
        DrawIcon(draw, *icon, ImVec2(min.x + pad, min.y + (s.metric.controlHeight - side) / 2), side, ink, dpi);
    draw->AddText(ImVec2(min.x + pad + lead, min.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2), ink, label);
    if (active)
        draw->AddRect(ImVec2(frameX, min.y), ImVec2(frameX + width, min.y + s.metric.controlHeight), Colour(s.accent.accent), s.radius.control, 0, dpi);
    return clicked;
}

bool TransportButton(const char* id, Icon icon, const char* label, const skin::Skin& s,
                     float dpi, bool primary = false, bool active = false,
                     std::initializer_list<const char*> reserve = {}) {
    return TransportBody(id, &icon, label, s, dpi, primary, active, reserve);
}

// Play, Pause and Cancel are one button. It has one width, and the mark
// beside Cancel is the one that means cancel.
bool PlayButton(const char* id, bool playing, int countdown, const skin::Skin& s, float dpi) {
    return TransportButton(id, playing ? Icon::Pause : countdown ? Icon::Close : Icon::Play,
        playing ? "Pause" : countdown ? "Cancel" : "Play", s, dpi, true, false, {"Play", "Pause", "Cancel"});
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
// A true minus sign for the back label, matching the transpose control; the
// hotkey hints use the hyphen because they are set in the monospace meta face.
std::string SeekLabel(int seconds, bool forward) {
    return (forward ? "+" : "\xe2\x88\x92") + std::to_string(seconds) + "s";
}

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

// A whole-number setting: its name, its value at the right, and the same
// groove the transpose and sustain controls use. Settings drew these three
// as stock ImGui sliders, a second slider style inside one window.
bool SettingSlider(const char* label, const char* id, int* value, int low, int high, const char* format,
                   const skin::Skin& s, float dpi) {
    char text[32]; snprintf(text, sizeof(text), format, *value);
    const float width = ImGui::GetContentRegionAvail().x;
    const float left = ImGui::GetCursorPosX();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(); ImGui::SetCursorPosX(left + width - ImGui::CalcTextSize(text).x);
    ImGui::PushStyleColor(ImGuiCol_Text, Colour(s.ink.secondary));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    float position = static_cast<float>(*value);
    Groove(id, &position, static_cast<float>(low), static_cast<float>(high), width, 22 * dpi, s, dpi, true);
    const int next = std::clamp(static_cast<int>(std::lround(position)), low, high);
    if (next == *value) return false;
    *value = next;
    return true;
}

// A section that opens, drawn like Velocity Response: a Lucide chevron and
// the label, inside the content's margins. ImGui's CollapsingHeader is a
// filled triangle on a bar wider than everything round it. Open state lives
// in the window's storage, as CollapsingHeader's did.
bool SettingSection(const char* label, const skin::Skin& s, float dpi) {
    auto* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID(label);
    bool open = storage->GetBool(key, false);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, s.metric.controlHeight);
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("##section", size)) { open = !open; storage->SetBool(key, open); }
    ImGui::PopID();
    auto* draw = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
        draw->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), Colour(s.surface.recessed), s.radius.control);
    const float side = 16 * dpi;
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    DrawIcon(draw, open ? Icon::Down : Icon::Right, ImVec2(min.x + 4 * dpi, min.y + (size.y - side) / 2), side, ink, dpi);
    draw->AddText(ImVec2(min.x + 4 * dpi + side + s.spacing.s2, min.y + (size.y - ImGui::GetTextLineHeight()) / 2), ink, label);
    return open;
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
               float dpi, float padding, bool enabled = true, const char* tip = nullptr,
               const char* stateText = nullptr) {
    auto s = skin::ScaleGeometry(design, dpi);
    // Sized in the on weight whatever the state. Sized by its current weight,
    // a pill switched on grew by the semibold difference and pushed every pill
    // after it to the right.
    float widest = 0;
    { FontScope bold(fonts, design, design.type.body * SpecFontScale(design), Weight::Semibold);
      widest = ImGui::CalcTextSize(label).x; }
    FontScope font(fonts, design, design.type.body * SpecFontScale(design), on ? Weight::Semibold : Weight::Regular);
    const auto min = ImGui::GetCursorScreenPos();
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 size(std::max(widest, text.x) + 2 * padding + 2 * dpi, s.metric.controlHeight);
    if (on) s.border.hairline = s.accent.okBorder;

    // One drawing path for every pill, interactive or not. Splitting them
    // between ImGui::Button and a hand-placed AddText put the semibold on
    // labels two pixels lower than the regular off ones, which read as the
    // text sitting unevenly inside the pill.
    bool clicked = false;
    // Under its own ID. The Velocity pill and the Velocity panel are both in
    // the shell window and both hashed the bare word, so activating the pill
    // from the keyboard also matched the panel.
    ImGui::PushID("pill");
    if (enabled) clicked = ImGui::InvisibleButton(label, size);
    else ImGui::Dummy(size);
    ImGui::PopID();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();

    ImU32 fill = on ? OpaqueTint(s.accent.okSoft, s.surface.structure) : Colour(s.surface.elevated);
    if (held) fill = Colour(s.surface.recessed);
    else if (hovered) fill = Colour(on ? s.accent.okSoft : s.surface.elevatedHot);
    // CSS clips the outside shadow at the control edge. Composite the tint
    // first so our stacked shadow cannot darken the translucent on surface.
    auto* dl = ImGui::GetWindowDrawList();
    skin::RaisedRect(dl, min, ImVec2(min.x + size.x, min.y + size.y), s.radius.control, s, fill);
    // A pill that cannot be pressed is drawn in the tertiary ink. In the
    // primary ink it was the same picture as one that is off, and a click on
    // it did nothing.
    dl->AddText(ImVec2(min.x + (size.x - text.x) / 2, min.y + (size.y - text.y) / 2),
                Colour(on ? s.accent.okInk : enabled ? s.ink.primary : s.ink.tertiary), label);

    // No underline under the label: at pill width it read as text decoration
    // rather than as a state. The on state is still not carried by colour
    // alone, because an on pill is semibold where an off pill is regular.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s: %s%s%s", label, stateText ? stateText : on ? "On" : "Off",
                          tip ? "\n" : "", tip ? tip : "");
    return clicked;
}

bool StatePills(const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine, bool compact) {
    const auto state = engine.Snapshot();
    const float pad = 8.f * dpi;
    if (StatePill("Midi2Key", state->liveActive, fonts, design, dpi, pad,
                  !state->liveDevice.empty(), nullptr))
        engine.Send({ShellEngine::Action::LiveActive, {}, 0, 0, !state->liveActive});
    ImGui::SameLine();
    const bool velocityAvailable = !state->outputMidi;
    // One label in both states. "Velocity unavailable" was half as wide again,
    // so switching the output to MIDI pushed every pill after it to the right.
    if (StatePill("Velocity", velocityAvailable && state->velocity, fonts, design, dpi, pad, velocityAvailable,
                  nullptr, velocityAvailable ? nullptr : "Unavailable"))
        engine.Send({ShellEngine::Action::Velocity, {}, 0, 0, !state->velocity});
    ImGui::SameLine();
    const bool sustainEnabled = !state->playing && !state->liveActive;
    if (StatePill("Sustain", state->sustain, fonts, design, dpi, pad, sustainEnabled,
                  nullptr))
        engine.Send({ShellEngine::Action::Sustain, {}, 0, 0, !state->sustain});
    ImGui::SameLine();
    if (StatePill(state->eightyEightKeys ? "88 Keys" : "61 Keys", state->eightyEightKeys,
                  fonts, design, dpi, pad, true, nullptr))
        engine.Send({ShellEngine::Action::EightyEightKeys, {}, 0, 0, !state->eightyEightKeys});
    ImGui::SameLine();
    if (StatePill("MidiConnect", state->midiConnect, fonts, design, dpi, pad, !state->liveDevice.empty(),
        nullptr))
        engine.Send({ShellEngine::Action::MidiConnect, {}, 0, 0, !state->midiConnect});
    {
        ImGui::SameLine();
        // Every other pill is its name, with the fill carrying on and off. This
        // one spelled its state out as well, so it read as a different kind of
        // control -- and the tooltip then said "AutoVol: off: off".
        return StatePill("AutoVol", state->autoVolume, fonts, design, dpi, pad, true,
            nullptr);
    }
    return false;
}

bool DevicePill(const std::string& name, float maxWidth, const skin::Skin& s, float dpi) {
    const auto min = ImGui::GetCursorScreenPos();
    const float width = std::min(maxWidth, ImGui::CalcTextSize(name.c_str()).x + 26 * dpi);
    skin::RaisedRect(ImGui::GetWindowDrawList(), min, ImVec2(min.x + width, min.y + s.metric.controlHeight),
                     s.radius.control, s, Colour(s.surface.card));
    DrawEllipsis(name, width - 24 * dpi, ImVec2(min.x + 12 * dpi,
        min.y + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    const bool clicked = ImGui::InvisibleButton("##device-pill", ImVec2(width, s.metric.controlHeight));
    // The name alone, for when the pill has cut it short. A click opens
    // Settings at the input, which is the instruction it used to print.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", name.c_str());
    return clicked;
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
    // A description says what the label cannot; a switch whose label is the
    // whole story passes none, and takes one row.
    if (description && *description) {
        FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
        ImGui::PushStyleColor(ImGuiCol_Text, Colour(s.ink.secondary));
        ImGui::TextWrapped("%s", description);
        ImGui::PopStyleColor();
    }
    return clicked;
}

// A radio drawn like the switch's rail. ImGui's own is a disc the height of
// a text field with a flat fill, and its edge reads as a polygon at 125%.
// This is a 16px ring with a dot, anti-aliased whatever the draw list's
// flags are at the time, and green the way the switch is green when on.
// Returns true on a click that changes the choice.
bool SettingRadio(const char* label, bool selected, const skin::Skin& design, float dpi) {
    const auto s = skin::ScaleGeometry(design, dpi);
    const float diameter = 16 * dpi;
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const auto min = ImGui::GetCursorScreenPos();
    const float height = s.metric.controlHeight;
    ImGui::PushID(label);
    // The hit area is invisible in every state: only the ring answers a
    // hover. With just the resting colours cleared, the button's own hover
    // and press fills drew a box wrapped tight around the label.
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    const bool clicked = ImGui::Button("##radio", ImVec2(diameter + 8 * dpi + labelSize.x, height));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    auto* draw = ImGui::GetWindowDrawList();
    const ImDrawListFlags flags = draw->Flags;
    draw->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;
    const ImVec2 centre(min.x + diameter / 2, min.y + height / 2);
    draw->AddCircleFilled(centre, diameter / 2, Colour(selected ? s.accent.okSoft : hovered ? s.surface.elevatedHot : s.surface.recessed));
    draw->AddCircle(centre, diameter / 2, Colour(selected ? s.accent.okBorder : s.border.strong), 0, dpi);
    if (selected) draw->AddCircleFilled(centre, 4 * dpi, Colour(s.accent.okInk));
    draw->Flags = flags;
    draw->AddText(ImVec2(min.x + diameter + 8 * dpi, min.y + (height - labelSize.y) / 2), Colour(s.ink.primary), label);
    return clicked && !selected;
}

// A tick box drawn like the radio: a 16px rounded square with the Lucide
// check inside, green when on the way the switch and the radio are green.
// ImGui's own is a flat square at the text field's height with a check
// that is a filled polygon, and it looked like a different application's.
// Returns true on a click, with value already toggled.
bool SettingCheck(const char* label, bool& value, const char* description,
                  const Fonts& fonts, const skin::Skin& design, float dpi) {
    const auto s = skin::ScaleGeometry(design, dpi);
    const float side = 16 * dpi;
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const auto min = ImGui::GetCursorScreenPos();
    const float height = s.metric.controlHeight;
    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    const bool clicked = ImGui::Button("##check", ImVec2(side + 8 * dpi + labelSize.x, height));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    if (clicked) value = !value;
    auto* draw = ImGui::GetWindowDrawList();
    const ImDrawListFlags flags = draw->Flags;
    draw->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;
    const ImVec2 box(min.x, min.y + (height - side) / 2);
    const ImVec2 boxEnd(box.x + side, box.y + side);
    draw->AddRectFilled(box, boxEnd, Colour(value ? s.accent.okSoft : hovered ? s.surface.elevatedHot : s.surface.recessed), 4 * dpi);
    draw->AddRect(box, boxEnd, Colour(value ? s.accent.okBorder : s.border.strong), 4 * dpi, 0, dpi);
    if (value) DrawIcon(draw, Icon::Check, ImVec2(box.x + 2 * dpi, box.y + 2 * dpi), side - 4 * dpi, Colour(s.accent.okInk), dpi);
    draw->Flags = flags;
    draw->AddText(ImVec2(min.x + side + 8 * dpi, min.y + (height - labelSize.y) / 2), Colour(s.ink.primary), label);
    if (description && *description) {
        FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
        ImGui::PushStyleColor(ImGuiCol_Text, Colour(s.ink.secondary));
        ImGui::TextWrapped("%s", description);
        ImGui::PopStyleColor();
    }
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

// IFileOpenDialog, as PickFolder already uses. GetOpenFileNameW is the legacy
// common dialog, and under this window -- layered for the opacity setting, and
// topmost whenever Always on top is on -- it returned without ever showing.
// The click reached here; nothing appeared. The two pickers now share an API.
enum class PickKind { Midi, Audio, Page };
std::filesystem::path PickFile(HWND hwnd, PickKind kind = PickKind::Midi) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    const COMDLG_FILTERSPEC midi[]{{L"MIDI files", L"*.mid;*.midi"}, {L"All files", L"*.*"}};
    const COMDLG_FILTERSPEC sound[]{{L"Audio files", L"*.mp3;*.wav;*.flac;*.ogg;*.oga;*.opus;*.m4a;*.aac"}, {L"All files", L"*.*"}};
    const COMDLG_FILTERSPEC page[]{{L"Saved sheet pages", L"*.html;*.htm"}, {L"All files", L"*.*"}};
    dialog->SetFileTypes(2, kind == PickKind::Audio ? sound : kind == PickKind::Page ? page : midi);
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

bool CopyUtf8ToClipboard(HWND hwnd, const std::string& text) {
    if (text.empty() || text.size() > static_cast<size_t>(INT_MAX)) return false;
    const int characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                static_cast<int>(text.size()), nullptr, 0);
    if (characters <= 0) return false;
    const size_t bytes = (static_cast<size_t>(characters) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    auto* destination = static_cast<wchar_t*>(GlobalLock(memory));
    if (!destination) { GlobalFree(memory); return false; }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), destination, characters);
    destination[characters] = L'\0';
    GlobalUnlock(memory);
    if (!OpenClipboard(hwnd)) { GlobalFree(memory); return false; }
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) {
        CloseClipboard();
        GlobalFree(memory);
        return false;
    }
    CloseClipboard();
    return true; // The clipboard owns memory after SetClipboardData succeeds.
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
        preferences.autoSolo = json.value("autoSoloPiano", false);
        preferences.alwaysOnTop = json.value("alwaysOnTop", false);
        preferences.opacity = std::clamp(json.value("opacity", 100), 40, 100);
        const auto folder = json.value("midiFolder", std::string());
        preferences.folder = std::filesystem::path(std::u8string(folder.begin(), folder.end()));
    } catch (const std::exception&) { preferences = {}; }
}

void Panels::SavePreferences(const std::filesystem::path& path) const {
    nlohmann::json json{{"skin", preferences.skin}, {"autoSoloPiano", preferences.autoSolo},
                        {"midiFolder", Utf8(preferences.folder)},
                        {"alwaysOnTop", preferences.alwaysOnTop}, {"opacity", preferences.opacity}};
    std::ofstream stream(path);
    if (stream) stream << json.dump(2) << '\n';
}

void Panels::DrawKeyMapping(const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    const bool platform = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    if (platform && mappingDpi_ > 0) dpi = mappingDpi_;
    const float height = 268.390625f;
    const auto* main = ImGui::GetMainViewport();
    if (!platform || mappingDpi_ == 0)
        ImGui::SetNextWindowPos(ImVec2(main->Pos.x + (main->Size.x - 840 * dpi) / 2, main->Pos.y + main->Size.y - (height + 40) * dpi));
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
    if (mappingLayout88_ != state->eightyEightKeys) {
        mappingLayout88_ = state->eightyEightKeys;
        mappingArmed_ = false;
        selectedNote_ = -1;
    }
    auto* draw = ImGui::GetWindowDrawList();
    const float title = 44 * dpi, width = 840 * dpi, pad = s.spacing.panelPad;
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
    // The selected note, an arrow, and the key it types as a keycap. Armed,
    // the cap is empty inside the accent ring, which is how a field waiting
    // for a key looks everywhere. It said so in a sentence before, and told
    // an idle window to "Click a key to remap".
    if (selectedNote_ >= 0) {
        FontScope font(fonts, design, design.type.meta * SpecFontScale(design));
        const auto found = state->keyMappings.find(NoteName(selectedNote_));
        const std::string key = mappingArmed_ || found == state->keyMappings.end() ? std::string() : found->second;
        const std::string note = NoteName(selectedNote_);
        const float line = ImGui::GetTextLineHeight(), textY = rowY + (s.metric.controlHeight - line) / 2;
        const float noteWidth = ImGui::CalcTextSize(note.c_str()).x, side = 14 * dpi;
        draw->AddText(at(pad, textY), Colour(s.ink.primary), note.c_str());
        DrawIcon(draw, Icon::Right, at(pad + noteWidth + s.spacing.s1, rowY + (s.metric.controlHeight - side) / 2), side, Colour(s.ink.tertiary), dpi);
        const float capX = pad + noteWidth + side + 2 * s.spacing.s1;
        const float capWidth = std::max(28 * dpi, ImGui::CalcTextSize(key.c_str()).x + 12 * dpi);
        const ImVec2 capMin = at(capX, textY - 3 * dpi), capMax = at(capX + capWidth, textY + line + 3 * dpi);
        draw->AddRectFilled(capMin, capMax, Colour(s.surface.elevated), 4 * dpi);
        draw->AddRect(capMin, capMax, Colour(mappingArmed_ ? s.accent.accent : s.border.hairline), 4 * dpi, 0, mappingArmed_ ? 2 * dpi : dpi);
        draw->AddText(at(capX + (capWidth - ImGui::CalcTextSize(key.c_str()).x) / 2, textY), Colour(s.ink.primary), key.c_str());
    }
    const float fullWidth = ImGui::CalcTextSize("Full 88").x + 24 * dpi;
    ImGui::SetCursorScreenPos(at(width - pad - 2 * s.metric.controlHeight - fullWidth - 16 * dpi, rowY));
    ImGui::BeginDisabled(fullKeyboard_ || rangeStart_ <= 21);
    if (IconButton("##lower-range", Icon::Left, "Lower range", s, dpi)) { rangeStart_ = std::max(21, rangeStart_ - 12); mappingArmed_ = false; }
    ImGui::EndDisabled(); ImGui::SameLine();
    const bool wasFull = fullKeyboard_;
    if (wasFull) ImGui::PushStyleColor(ImGuiCol_Button, Colour(s.accent.accentSoft));
    if (ImGui::Button("Full 88", ImVec2(fullWidth, s.metric.controlHeight))) { fullKeyboard_ = !fullKeyboard_; mappingArmed_ = false; }
    if (wasFull) ImGui::PopStyleColor();
    // The same accent ring every other toggle in the app wears, rather than
    // the checked corner this used to draw. Both carry the state without
    // colour; only one of them means "on" everywhere else you look.
    if (fullKeyboard_)
        draw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                      Colour(s.accent.accent), s.radius.control, 0, dpi);
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
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                (state->eightyEightKeys || (hoveredNote >= 36 && hoveredNote <= 96))) {
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
          std::string(state->eightyEightKeys ? "Layout 88-key     Range " : "Layout 61-key     Range ") +
          NoteName(whites.front()) + "\xe2\x80\x93" + NoteName(whites.back());
      Ellipsis(status, width - 2 * pad); }
    ImGui::PopFont();
    ImGui::GetStyle() = previousStyle;
    ImGui::End();
}

void Panels::DrawAutoVolume(const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    using A = ShellEngine::Action;
    const auto state = engine.Snapshot();
    const bool pending = state->autoVolumeCountdown > 0 || state->autoVolumeFocusing;
    if (!autoVolumeOpen) {
        if (volumeWasOpen_ && pending) engine.Send({A::AutoVolumeCancel});
        volumeWasOpen_ = false;
        return;
    }
    if (!volumeWasOpen_) {
        engine.Send({A::AutoVolumeScan});
        volumeWindow_ = state->volumeTarget;
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Pos.x + 24 * dpi,
                                      ImGui::GetMainViewport()->Pos.y + 112 * dpi));
        volumeWasOpen_ = true;
    }
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::SetNextWindowSize(ImVec2(440 * dpi, 0));
    const auto s = skin::ScaleGeometry(design, dpi);
    FontScope font(fonts, design, design.type.body * SpecFontScale(design));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * dpi, 16 * dpi));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    if (ImGui::Begin("AutoVol", &autoVolumeOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::Spacing();
        if (state->autoVolume) {
            ImGui::TextUnformatted("AutoVol is on");
            ImGui::TextWrapped("Calibrated for: %s", state->volumeTarget.title.c_str());
        } else if (pending) {
            if (state->autoVolumeFocusing) ImGui::TextUnformatted("Focusing the selected game...");
            else ImGui::Text("Calibration starts in %d", state->autoVolumeCountdown);
        } else if (state->autoVolumeNeedsCalibration) ImGui::TextUnformatted("AutoVol is off until it is calibrated");
        else ImGui::TextUnformatted("AutoVol is off");
        ImGui::Spacing();
        ImGui::BeginDisabled(pending);
        ImGui::TextUnformatted("Game window");
        ImGui::SetNextItemWidth(-s.metric.controlHeight - 8 * dpi);
        const bool selected = std::any_of(state->volumeWindows.begin(), state->volumeWindows.end(), [&](const auto& window) {
            return window.id == volumeWindow_.id && window.process == volumeWindow_.process && window.title == volumeWindow_.title;
        });
        if (ImGui::BeginCombo("##volume-window", selected ? volumeWindow_.title.c_str() : "Select the game window", ImGuiComboFlags_NoArrowButton)) {
            for (const auto& window : state->volumeWindows) {
                ImGui::PushID(reinterpret_cast<void*>(window.id));
                if (ImGui::Selectable(window.title.c_str(), window.id == volumeWindow_.id)) volumeWindow_ = window;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ComboChevron(); ImGui::SameLine();
        if (IconButton("##volume-refresh", Icon::Refresh, "Refresh game windows", s, dpi)) engine.Send({A::AutoVolumeScan});
        ImGui::Spacing();
        ImGui::BeginDisabled(!selected);
        if (ImGui::Button("Focus game and calibrate", ImVec2(-1, s.metric.controlHeight))) {
            ShellEngine::Command command{A::AutoVolumeCalibrate, {}, state->generation};
            command.window = volumeWindow_;
            engine.Send(std::move(command));
        }
        // The button says what it does, and the countdown above shows it
        // happening. The key-by-key account that hung off it as a tooltip was
        // a paragraph of explanation, and belongs to the help page.
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (pending) {
            if (ImGui::Button("Cancel calibration", ImVec2(-1, s.metric.controlHeight))) engine.Send({A::AutoVolumeCancel});
        } else if (state->autoVolume || state->autoVolumeNeedsCalibration) {
            if (ImGui::Button("Turn AutoVol off", ImVec2(-1, s.metric.controlHeight))) engine.Send({A::AutoVolumeOff});
        }
        if (!state->error.empty()) ImGui::TextWrapped("%s", state->error.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    if (!autoVolumeOpen && pending) engine.Send({A::AutoVolumeCancel});
}

std::function<std::filesystem::path(HWND)> PickMidiFile = [](HWND hwnd) { return PickFile(hwnd); };

Panels::~Panels() { if (measuring_) input_latency::stop(); }

ImVec2 Panels::DesiredSize() const {
    // Mini has no panel that stretches, so both sides are the content. The
    // width is the state pills and a window pad each side; 640 left a blank
    // fifth of the window to their right. The height is the 88dpi strip, the
    // rows at 8dpi apart, and the status bar.
    if (miniMode) return ImVec2(528, miniAutoplay ? 256.f : 164.f);
    return ImVec2(940, velocityExpanded ? 974.f : 600.f);
}

namespace {
const char* BackendName(MidiBackend backend) {
    switch (backend) {
    case MidiBackend::WinMM: return "WinMM";
    case MidiBackend::KernelStreaming: return "Kernel Streaming";
    case MidiBackend::WootingAnalog: return "Wooting Analog";
    default: return "WinRT";
    }
}
std::string DeviceName(const EngineSnapshot& state) {
    if (state.liveDevice.empty()) return "No MIDI input";
    for (const auto& device : state.devices) if (device.id == state.liveDevice) return device.name;
    return "Connected MIDI input";
}
std::string OutputDeviceName(const EngineSnapshot& state) {
    if (state.outputDevice.empty()) return "No MIDI output";
    for (const auto& device : state.outputDevices) if (device.id == state.outputDevice) return device.name;
    return "Connected MIDI output";
}
void CurveCombo(const char* id, float width, const EngineSnapshot& state, ShellEngine& engine) {
    ImGui::SetNextItemWidth(width);
    const auto& edit = state.comparingCurve ? state.previousCurve : state.curve;
    // The preview is cut to the room left of the chevron, which is drawn over
    // the last .8 of a control height. The frame clips at its own edge, not
    // the chevron's, so "Linear Coarse (edited)" ran underneath it.
    std::string preview = state.ActiveVelocityName();
    const float room = width - 2 * ImGui::GetStyle().FramePadding.x - ImGui::GetFrameHeight() * .8f;
    if (ImGui::CalcTextSize(preview.c_str()).x > room) {
        while (!preview.empty() && ImGui::CalcTextSize((preview + "...").c_str()).x > room) {
            // One character, which in UTF-8 is its continuation bytes and
            // then the byte that leads them.
            while (!preview.empty()) {
                const unsigned char last = static_cast<unsigned char>(preview.back());
                preview.pop_back();
                if ((last & 0xC0) != 0x80) break;
            }
        }
        preview += "...";
    }
    const bool curveOpen = ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_NoArrowButton);
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

std::vector<float> PlayedVelocityTargets(const velocity_telemetry::Snapshot& played) {
    if (!played.total) return {};
    std::vector<float> targets;
    const auto quantile = [&](float fraction) {
        const uint32_t target = std::max(1u, static_cast<uint32_t>(std::ceil(played.total * fraction)));
        uint32_t count = 0;
        for (size_t i = 0; i < played.buckets.size(); ++i) {
            count += played.buckets[i];
            if (count >= target) return std::clamp((static_cast<float>(i) + .5f) * 4.f / 127.f, 0.f, 1.f);
        }
        return 1.f;
    };
    for (const float fraction : {.1f, .5f, .9f}) {
        const float value = quantile(fraction);
        if (targets.empty() || std::abs(value - targets.back()) > .02f) targets.push_back(value);
    }
    return targets;
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
    float comboEnd = start.x;
    if (!expanded) {
        ImGui::SameLine();
        // Never narrower than its longest preset name and the chevron: at the
        // smallest window the old floor put the chevron over "Linear Fine".
        float longest = 0;
        for (const auto& curve : state->curves) longest = std::max(longest, ImGui::CalcTextSize(curve.name.c_str()).x);
        // ComboChevron draws within the last .78 of the control height.
        // And never so wide that it pushes Sustain cutoff off the panel: a
        // custom curve's name may run to 120 bytes. CurveCombo cuts the
        // preview to fit.
        const float floor = std::min(longest + 2 * ImGui::GetStyle().FramePadding.x + control * .8f, width * .42f);
        CurveCombo("##collapsed-curve", std::max(floor, width - 430 * dpi), *state, engine);
        comboEnd = ImGui::GetItemRectMax().x;
    }
    // Sustain cutoff takes what is left; its groove shrinks first, to 40px.
    float cutoffLabelWidth = 0;
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design)); cutoffLabelWidth = ImGui::CalcTextSize("Sustain cutoff").x; }
    const float cutoffValueWidth = ImGui::CalcTextSize("127").x;
    const float cutoffX = std::max(comboEnd + s.spacing.s3, start.x + width - 248 * dpi);
    const float grooveWidth = std::clamp(start.x + width - cutoffX - cutoffLabelWidth - cutoffValueWidth - 2 * ImGui::GetStyle().ItemSpacing.x,
                                         40 * dpi, 120 * dpi);
    ImGui::SetCursorScreenPos(ImVec2(cutoffX, start.y));
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design)); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Sustain cutoff"); }
    ImGui::SameLine();
    // Commit on release, just like the macro sliders.
    float cutoff = cutoffEditing_ ? cutoffPreview_ : static_cast<float>(state->sustainCutoff);
    if (Groove("##sustain-cutoff", &cutoff, 0, 127, grooveWidth, control, s, dpi, true)) {
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
        // A gesture in flight was editing the curve this replaced. Carried
        // on, it would commit the dragged point against an empty anchor list.
        curveGesture_ = false; activeAnchor_ = -1; freeDraw_.clear();
    }
    editorError_ = state->error;
    const auto openName = [&](int operation) {
        nameOperation_ = operation; focusCurveName_ = true; nameRevision_ = state->curveRevision;
        auto name = operation == 1 ? "New Curve" : state->curves[state->curve.preset].name;
        if (operation == 2 || (operation == 3 && state->curve.preset < midi::kBuiltinVelocityCurves)) name += " Copy";
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
    const float graphHeight = 208 * dpi;
    const auto& shown = state->comparingCurve ? state->previousCurve : editor_;
    const auto& preset = state->comparingCurve ? state->previousPreset : state->curves[shown.preset];
    const ImVec2 graphMin(start.x, workspaceY), graphMax(start.x + mainWidth, workspaceY + graphHeight);
    auto* draw = ImGui::GetWindowDrawList();
    skin::RecessedRect(draw, graphMin, graphMax, s.radius.element, s);
    const ImVec2 plotMin(graphMin.x + 12 * dpi, graphMin.y + 40 * dpi), plotMax(graphMax.x - 12 * dpi, graphMax.y - 28 * dpi);

    // The graph tools are the editor's primary controls. Selection has an
    // outline as well as accent colour, and history uses conventional icons.
    ImGui::SetCursorScreenPos(ImVec2(graphMin.x + 8 * dpi, graphMin.y + 4 * dpi));
    ImGui::BeginDisabled(state->comparingCurve);
    if (IconButton("##anchor-tool", Icon::Anchor, "Edit anchors", s, dpi, curveTool_ == 0)) curveTool_ = 0;
    ImGui::SameLine();
    if (IconButton("##draw-tool", Icon::Rename, "Free draw", s, dpi, curveTool_ == 1)) curveTool_ = 1;
    ImGui::SameLine(0, 14 * dpi);
    ImGui::BeginDisabled(!state->canUndoCurve);
    if (IconButton("##curve-undo", Icon::Undo, "Undo curve edit", s, dpi)) engine.Send({ShellEngine::Action::CurveUndo});
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!state->canRedoCurve);
    if (IconButton("##curve-redo", Icon::Redo, "Redo curve edit", s, dpi)) engine.Send({ShellEngine::Action::CurveRedo});
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    // Only with the main window focused. Key Mapping assigns "ctrl+z" as a
    // binding, and that press must not also undo a curve behind it.
    if (!state->comparingCurve && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z))
            engine.Send({ImGui::GetIO().KeyShift ? ShellEngine::Action::CurveRedo : ShellEngine::Action::CurveUndo});
        else if (ImGui::IsKeyPressed(ImGuiKey_Y)) engine.Send({ShellEngine::Action::CurveRedo});
    }

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
    // The unchanged response, as one more hairline of the grid rather than
    // a dashed line in the ink colour: it is a reference, not a series.
    draw->AddLine(ImVec2(plotMin.x, plotMax.y), ImVec2(plotMax.x, plotMin.y), Colour(s.border.hairline), dpi);

    // The velocities played most, which an anchor snaps to. They are drawn
    // only while an anchor is being dragged (below), because at rest they
    // were dashed lines and arrows on a graph that already had a grid, a
    // fill and a curve.
    const auto snapTargets = PlayedVelocityTargets(state->playedVelocities);

    ImGui::SetCursorScreenPos(plotMin);
    ImGui::BeginDisabled(state->comparingCurve);
    ImGui::InvisibleButton("##curve-graph", ImVec2(plotMax.x - plotMin.x, plotMax.y - plotMin.y));
    const auto mousePoint = [&] {
        return VelocityPoint{
            std::clamp((ImGui::GetIO().MousePos.x - plotMin.x) / (plotMax.x - plotMin.x), 0.f, 1.f),
            std::clamp((plotMax.y - ImGui::GetIO().MousePos.y) / (plotMax.y - plotMin.y), 0.f, 1.f)};
    };
    const auto snapX = [&](float x) {
        float result = x, distance = 10 * dpi / (plotMax.x - plotMin.x);
        for (const float target : snapTargets) if (std::abs(target - x) <= distance) {
            result = target; distance = std::abs(target - x);
        }
        return result;
    };
    if (ImGui::IsItemActivated()) {
        curveGestureBase_ = editor_;
        curveGestureBase_.anchors = VelocityAnchorsFor(preset, editor_);
        curveGestureBase_.sensitivity = curveGestureBase_.contrast = 0;
        editor_ = curveGestureBase_;
        curveGesture_ = false;
        if (curveTool_ == 0) {
            const auto mouse = mousePoint();
            float nearest = 10 * dpi; activeAnchor_ = -1;
            for (size_t i = 0; i < editor_.anchors.size(); ++i) {
                const float dx = (editor_.anchors[i].x - mouse.x) * (plotMax.x - plotMin.x);
                const float dy = (editor_.anchors[i].y - mouse.y) * (plotMax.y - plotMin.y);
                const float distance = std::hypot(dx, dy);
                if (distance < nearest) { nearest = distance; activeAnchor_ = static_cast<int>(i); }
            }
            const float curveY = VelocityShape(preset, editor_, mouse.x);
            if (activeAnchor_ < 0 && std::abs(curveY - mouse.y) * (plotMax.y - plotMin.y) <= 10 * dpi)
                activeAnchor_ = static_cast<int>(VelocityAddAnchor(editor_.anchors, snapX(mouse.x), curveY));
            curveGesture_ = activeAnchor_ >= 0;
            // The drag moves this anchor in this list, every frame. Moving it
            // in last frame's result let a merge at a shared x shift the
            // index onto a neighbour, and kept every flattening a drag passed
            // through on the way to where it ended.
            dragAnchors_ = editor_.anchors; dragIndex_ = activeAnchor_;
        } else {
            freeDraw_.clear(); freeDraw_.push_back(mousePoint()); curveGesture_ = true;
        }
    }
    if (ImGui::IsItemActive() && curveGesture_) {
        auto point = mousePoint();
        if (curveTool_ == 0 && activeAnchor_ >= 0) {
            const auto mouse = ImGui::GetIO().MousePos;
            const bool inside = mouse.x >= plotMin.x && mouse.x <= plotMax.x && mouse.y >= plotMin.y && mouse.y <= plotMax.y;
            if (inside) {
                point.x = snapX(point.x);
                editor_.anchors = dragAnchors_;
                activeAnchor_ = static_cast<int>(VelocityMoveAnchor(editor_.anchors, dragIndex_, point));
            }
        } else if (curveTool_ == 1) {
            const auto& last = freeDraw_.back();
            const float dx = (last.x - point.x) * (plotMax.x - plotMin.x);
            const float dy = (last.y - point.y) * (plotMax.y - plotMin.y);
            if (std::hypot(dx, dy) >= 2 * dpi) freeDraw_.push_back(point);
        }
    }
    if (ImGui::IsItemActive() && curveGesture_ && curveTool_ == 0)
        for (const float target : snapTargets) {
            const float x = plotMin.x + target * (plotMax.x - plotMin.x);
            draw->AddLine(ImVec2(x, plotMin.y), ImVec2(x, plotMax.y), Colour(s.accent.accentSoft), 2 * dpi);
        }
    if (ImGui::IsItemDeactivated() && curveGesture_) {
        if (curveTool_ == 0 && activeAnchor_ >= 0) {
            const auto mouse = ImGui::GetIO().MousePos;
            const bool outside = mouse.x < plotMin.x || mouse.x > plotMax.x || mouse.y < plotMin.y || mouse.y > plotMax.y;
            if (outside && editor_.anchors.size() > 2 && activeAnchor_ > 0 &&
                activeAnchor_ + 1 < static_cast<int>(editor_.anchors.size()))
                editor_.anchors.erase(editor_.anchors.begin() + activeAnchor_);
        } else if (curveTool_ == 1 && freeDraw_.size() >= 2) {
            editor_.anchors = VelocityApplySweep(curveGestureBase_.anchors, freeDraw_);
        }
        // A click that moved nothing is not an edit. Sent anyway, it turned
        // an untouched built-in into "(edited)", stopped playback and reopened
        // the MIDI device, for a curve nobody changed.
        const auto& before = curveGestureBase_.anchors;
        const bool changed = editor_.anchors.size() != before.size() ||
            !std::equal(before.begin(), before.end(), editor_.anchors.begin(), [](const VelocityPoint& a, const VelocityPoint& b) {
                return std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f; });
        if (changed) {
            ShellEngine::Command command{ShellEngine::Action::CurveEdit};
            command.anchors = editor_.anchors; engine.Send(std::move(command));
        } else editor_ = state->curve;
        activeAnchor_ = -1; freeDraw_.clear(); curveGesture_ = false;
    } else if (ImGui::IsItemDeactivated()) {
        // Pressed where there was nothing to take hold of. The press baked the
        // preset into anchors and zeroed both sliders; put that back.
        editor_ = state->curve;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(curveTool_ == 0 ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeAll);
    }
    ImGui::EndDisabled();

    // The 32 steps the game receives, as one soft fill whose top edge is the
    // staircase. The steps used to be outlined as well, which put a second
    // grey line under the curve everywhere the two did not coincide.
    const auto thresholds = VelocityThresholds(preset, shown);
    int previousThreshold = 0;
    const auto ghost = Colour((s.ink.tertiary & 0x00ffffffu) | 0x22000000u);
    for (int bucket = 0; bucket < 32; ++bucket) {
        const int edge = thresholds[bucket];
        if (edge <= previousThreshold) continue;
        const float x0 = plotMin.x + previousThreshold / 127.f * (plotMax.x - plotMin.x);
        const float x1 = plotMin.x + edge / 127.f * (plotMax.x - plotMin.x);
        const float y = plotMax.y - bucket / 31.f * (plotMax.y - plotMin.y);
        draw->AddRectFilled(ImVec2(x0, y), ImVec2(x1, plotMax.y), ghost);
        previousThreshold = edge;
    }
    DrawCurveLine(draw, preset, shown, plotMin, plotMax, Colour(s.accent.accent), 2 * dpi);
    if (!state->comparingCurve && curveTool_ == 0) for (size_t i = 0; i < editor_.anchors.size(); ++i) {
        const auto& anchor = editor_.anchors[i];
        const ImVec2 point(plotMin.x + anchor.x * (plotMax.x - plotMin.x),
                           plotMax.y - anchor.y * (plotMax.y - plotMin.y));
        const float radius = static_cast<int>(i) == activeAnchor_ ? 6 * dpi : 4.5f * dpi;
        draw->AddCircleFilled(point, radius, Colour(s.surface.elevated));
        draw->AddCircle(point, radius, Colour(s.accent.accent), 16, static_cast<int>(i) == activeAnchor_ ? 2 * dpi : dpi);
    }
    if (curveTool_ == 1 && curveGesture_ && freeDraw_.size() > 1) {
        for (const auto& point : freeDraw_)
            draw->PathLineTo(ImVec2(plotMin.x + point.x * (plotMax.x - plotMin.x),
                                    plotMax.y - point.y * (plotMax.y - plotMin.y)));
        draw->PathStroke(Colour(s.accent.accent), 0, 2.5f * dpi);
    }
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
        if (changed) editor_.anchors.clear();
        if (ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive()))
            engine.Send({ShellEngine::Action::CurveAdjust, {}, 0, 0, false, *target, i ? "contrast" : "sensitivity"});
    }
    ImGui::EndDisabled();
    const float mainBottom = macroY + 80 * dpi;
    const ImVec2 listMin(start.x + mainWidth + 12 * dpi, workspaceY);
    skin::RecessedRect(draw, listMin, ImVec2(start.x + width, mainBottom), s.radius.element, s);
    ImGui::SetCursorScreenPos(ImVec2(listMin.x + 8 * dpi, listMin.y + 8 * dpi));
    { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design)); ImGui::TextUnformatted("Starting points"); }
    ImGui::SetCursorScreenPos(ImVec2(listMin.x + 4 * dpi, listMin.y + 32 * dpi));
    // Rows sit flush: with the body's item spacing between them the six
    // built-ins ran a row and a half past the list and S-Curve was half
    // hidden behind a scrollbar at every size. Custom curves still scroll.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0));
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
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s", state->curves[i].name.c_str(), i >= midi::kBuiltinVelocityCurves ? " (custom)" : "");
        if (selected && listRevision_ != state->curveRevision) ImGui::SetScrollHereY(.5f);
        ImGui::PopID();
    }
    listRevision_ = state->curveRevision;
    ImGui::EndChild();
    ImGui::PopStyleVar();
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
    section("MIDI input");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    const auto groups = GroupDevices(state->devices);
    const auto* selectedGroup = SelectedGroup(groups, state->liveDevice);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - s.metric.controlHeight - 8 * dpi);
    const bool deviceOpen = ImGui::BeginCombo("##midi-input", DeviceName(*state).c_str(), ImGuiComboFlags_NoArrowButton);
    ComboChevron();
    if (deviceOpen) {
        if (ImGui::Selectable("No MIDI input", state->liveDevice.empty())) engine.Send({ShellEngine::Action::LiveOpen});
        for (size_t i = 0; i < groups.size(); ++i) {
            const auto& group = groups[i];
            ImGui::PushID(static_cast<int>(i));
            const auto name = group.name + (group.ambiguous ? " (port " + std::to_string(i + 1) + ")" : "");
            if (ImGui::Selectable(name.c_str(), selectedGroup == &group)) {
                ShellEngine::Command command{ShellEngine::Action::LiveOpen};
                command.device = PreferredInput(group, state->liveDevice); engine.Send(std::move(command));
            }
            if (group.ambiguous && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s", BackendName(group.inputs.front().backend),
                    Utf8(std::filesystem::path(group.inputs.front().id)).c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (IconButton("##scan-midi", Icon::Refresh, "Scan MIDI inputs", s, dpi)) engine.Send({ShellEngine::Action::LiveScan});
    if (selectedGroup) {
        ImGui::TextUnformatted("Transport");
        ImGui::SetNextItemWidth(-1);
        const bool transportOpen = ImGui::BeginCombo("##device-transport", BackendName(BackendForDeviceId(state->liveDevice)), ImGuiComboFlags_NoArrowButton);
        ComboChevron();
        if (transportOpen) {
            for (const auto& input : selectedGroup->inputs) {
                if (ImGui::Selectable(BackendName(input.backend), input.id == state->liveDevice)) {
                    ShellEngine::Command command{ShellEngine::Action::LiveOpen}; command.device = input.id; engine.Send(std::move(command));
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Separator();
    section("MIDI output");
    if (SettingRadio("Keystrokes", !state->outputMidi, design, dpi))
        engine.Send({ShellEngine::Action::OutputTarget, {}, 0, 0, false});
    ImGui::SameLine(0, 16 * dpi);
    if (SettingRadio("MIDI", state->outputMidi, design, dpi))
        engine.Send({ShellEngine::Action::OutputTarget, {}, 0, 0, true});

    const auto outputGroups = GroupDevices(state->outputDevices);
    const auto* selectedOutputGroup = SelectedGroup(outputGroups, state->outputDevice);
    ImGui::BeginDisabled(!state->outputMidi);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - s.metric.controlHeight - 8 * dpi);
    const bool outputOpen = ImGui::BeginCombo("##midi-output", OutputDeviceName(*state).c_str(), ImGuiComboFlags_NoArrowButton);
    ComboChevron();
    if (outputOpen) {
        if (ImGui::Selectable("No MIDI output", state->outputDevice.empty()))
            engine.Send({ShellEngine::Action::OutputOpen});
        for (size_t i = 0; i < outputGroups.size(); ++i) {
            const auto& group = outputGroups[i];
            ImGui::PushID(static_cast<int>(i));
            const auto name = group.name + (group.ambiguous ? " (port " + std::to_string(i + 1) + ")" : "");
            if (ImGui::Selectable(name.c_str(), selectedOutputGroup == &group)) {
                ShellEngine::Command command{ShellEngine::Action::OutputOpen};
                command.device = PreferredInput(group, state->outputDevice); engine.Send(std::move(command));
            }
            if (group.ambiguous && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s", BackendName(group.inputs.front().backend),
                    Utf8(std::filesystem::path(group.inputs.front().id)).c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (IconButton("##scan-midi-output", Icon::Refresh, "Scan MIDI outputs", s, dpi))
        engine.Send({ShellEngine::Action::OutputScan});
    if (selectedOutputGroup) {
        ImGui::BeginDisabled(!state->outputMidi);
        ImGui::TextUnformatted("Transport");
        ImGui::SetNextItemWidth(-1);
        const bool outputTransportOpen = ImGui::BeginCombo("##output-transport",
            BackendName(BackendForOutputId(state->outputDevice)), ImGuiComboFlags_NoArrowButton);
        ComboChevron();
        if (outputTransportOpen) {
            for (const auto& output : selectedOutputGroup->inputs) {
                if (ImGui::Selectable(BackendName(output.backend), output.id == state->outputDevice)) {
                    ShellEngine::Command command{ShellEngine::Action::OutputOpen};
                    command.device = output.id; engine.Send(std::move(command));
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
    }
    ImGui::Separator();
    const bool wootingSelected = state->liveDevice == L"wooting:analog" &&
        BackendForDeviceId(state->liveDevice) == MidiBackend::WootingAnalog;
    if (wootingSelected) {
        ImGui::Separator();
        section("Wooting Analog");
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
        setting(2, "Velocity scale", "##wooting-velocity", .1f, 20.f, .1f,
                ShellEngine::Action::WootingVelocityScale, "%.1f");
        ImGui::Separator();
    } else {
        wootingEditing_.fill(false);
        wootingPending_.fill(false);
    }
    bool active = state->liveActive;
    ImGui::BeginDisabled(state->liveDevice.empty());
    if (SettingCheck("Midi2Key", active, nullptr, fonts, design, dpi))
        engine.Send({ShellEngine::Action::LiveActive, {}, 0, 0, active});
    ImGui::EndDisabled();
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
    // Closed by default: it is a diagnostic, and open it was a screen of the
    // scroll. A measurement keeps running with the header closed.
    if (SettingSection("Keyboard timing", s, dpi)) {
    bool measure = measuring_;
    if (SettingCheck("Measure keyboard timing", measure, nullptr, fonts, design, dpi)) {
        if (measure) { timing_ = input_latency::Collector{}; timingSummary_ = {}; measuring_ = input_latency::start(); }
        else { input_latency::stop(); measuring_ = false; timingSummary_ = {}; }
    }
    if (!measuring_ && input_latency::hookError()) ImGui::Text("Hook error %lu", input_latency::hookError());
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
        }
        ImGui::Text("%zu incomplete   %llu failures   %llu dropped", t.incomplete,
            static_cast<unsigned long long>(t.failures), static_cast<unsigned long long>(input_latency::dropped()));
    }
    }
    ImGui::Separator();
    section("Behaviour");
    ImGui::BeginDisabled(state->eightyEightKeys);
    bool outRange = state->outRange;
    if (SettingSwitch("Fold out-of-range notes onto the keys", outRange,
        nullptr, fonts, design, dpi))
        engine.Send({ShellEngine::Action::OutRange, {}, 0, 0, outRange});
    ImGui::EndDisabled();
    int playbackDelay = state->playbackDelay;
    if (SettingSlider("Play button countdown", "##playback-delay", &playbackDelay, 0, 10, "%d seconds", s, dpi))
        engine.Send({ShellEngine::Action::PlaybackDelay, {}, 0, 0, false, static_cast<double>(playbackDelay)});
    int seekStep = state->seekStep;
    if (SettingSlider("Skip step", "##seek-step", &seekStep, 1, 60, "%d seconds", s, dpi))
        engine.Send({ShellEngine::Action::SeekStep, {}, 0, 0, false, static_cast<double>(seekStep)});
    // On and off are already on the pill. Calibration pending is not, and it is
    // the one state that wants something from you, so it keeps its suffix.
    if (ImGui::Button(state->autoVolumeNeedsCalibration ? "AutoVol: calibrate" : "AutoVol", ImVec2(-1, s.metric.controlHeight))) {
        autoVolumeOpen = true;
        ImGui::CloseCurrentPopup();
    }
    SettingSwitch("Solo piano tracks on load", preferences.autoSolo, nullptr, fonts, design, dpi);
    bool legit = state->legitMode;
    if (SettingSwitch("Legit Mode", legit,
        nullptr, fonts, design, dpi))
        engine.Send({ShellEngine::Action::LegitMode, {}, 0, 0, legit});
    bool shuffle = state->shuffle;
    if (SettingSwitch("Shuffle Play", shuffle, nullptr, fonts, design, dpi))
        engine.Send({ShellEngine::Action::Shuffle, {}, 0, 0, shuffle});
    if (revealSettingsSwitches) ImGui::SetScrollHereY(0.f);
    bool detectDrums = state->detectDrums;
    if (SettingSwitch("Detect drum tracks", detectDrums, nullptr, fonts, design, dpi))
        engine.Send({ShellEngine::Action::DetectDrums, {}, 0, 0, detectDrums});
    bool autoTranspose = state->autoTranspose;
    if (SettingSwitch("Auto-transpose on load", autoTranspose, nullptr, fonts, design, dpi))
        engine.Send({ShellEngine::Action::AutoTranspose, {}, 0, 0, autoTranspose});
    bool velocity = state->velocity;
    const std::string modifierName = state->velocityModifier == "ctrl" ? "Ctrl" :
        state->velocityModifier == "shift" ? "Shift" : "Alt";
    ImGui::BeginDisabled(state->outputMidi);
    if (SettingSwitch("Velocity hotkeys", velocity,
        nullptr, fonts, design, dpi))
        engine.Send({ShellEngine::Action::Velocity, {}, 0, 0, velocity});
    ImGui::EndDisabled();
    ImGui::TextUnformatted("Velocity modifier");
    ImGui::SetNextItemWidth(-1);
    const bool modifierOpen = ImGui::BeginCombo("##velocity-modifier", modifierName.c_str(), ImGuiComboFlags_NoArrowButton);
    ComboChevron();
    if (modifierOpen) {
        for (const auto& [value, label] : {std::pair{"alt", "Alt"}, std::pair{"ctrl", "Ctrl"}, std::pair{"shift", "Shift"}}) {
            if (ImGui::Selectable(label, state->velocityModifier == value)) {
                ShellEngine::Command command{ShellEngine::Action::VelocityModifier};
                command.key = value; engine.Send(std::move(command));
            }
        }
        ImGui::EndCombo();
    }
    if (!state->velocityModifierConflicts.empty()) {
        // The combinations this modifier shares with mapped notes, in the
        // warning colour under the control that caused them. The sentence
        // round them explained; the list is the fact.
        std::string warning;
        for (size_t i = 0; i < state->velocityModifierConflicts.size(); ++i) {
            if (i) warning += "   ";
            warning += state->velocityModifierConflicts[i];
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Colour(s.accent.warn));
        ImGui::TextWrapped("%s", warning.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::BeginDisabled(!state->hasPreviousCurve);
    if (SettingSection("Curve comparison", s, dpi)) {
        if (ImGui::Button(state->comparingCurve ? "Return to edited curve" : "Hear previous curve",
                          ImVec2(-1, s.metric.controlHeight)))
            engine.Send({ShellEngine::Action::CurveCompare});
    }
    ImGui::EndDisabled();
    SettingSwitch("Always on top", preferences.alwaysOnTop, nullptr, fonts, design, dpi);
    ImGui::Separator();
    section("Appearance");
    SettingSlider("Window opacity", "##window-opacity", &preferences.opacity, 40, 100, "%d%%", s, dpi);
    // The names come from the skins themselves. They were spelled out here as
    // "Classic" and "Modern", which is two more places to rename and two more
    // chances for the picker to disagree with what it picks.
    // Skin::name is a string_view, so it is copied rather than .data()'d: a
    // view is not required to be null-terminated, and ImGui wants a C string.
    const auto skins = skin::All();
    const std::string firstColour(skins[0].name), secondColour(skins[2].name);
    if (SettingRadio(firstColour.c_str(), preferences.skin < 2, design, dpi)) preferences.skin %= 2;
    ImGui::SameLine(0, 16 * dpi);
    if (SettingRadio(secondColour.c_str(), preferences.skin >= 2, design, dpi)) preferences.skin = 2 + preferences.skin % 2;
    if (SettingSection("About", s, dpi)) {
        ImGui::TextWrapped("Based on Zephkek/MIDIPlusPlus (GPLv3)");
        ImGui::TextWrapped("Dear ImGui and RtMidi (MIT)");
        ImGui::TextWrapped("Sheet notation from ArijanJ/midi-converter (MIT)");
        ImGui::TextWrapped("IBM Plex Sans (SIL Open Font License 1.1)");
    }
    ImGui::PopStyleVar();
}

void Panels::SettingsControl(const Fonts& fonts, const skin::Skin& design, float dpi,
                             ShellEngine& engine, ImVec2 popupPosition, float popupMaxHeight) {
    const auto s = skin::ScaleGeometry(design, dpi);
    // Marked while the popover is open. It was the one control in the strip
    // that gave no sign it had been pressed: the panel appeared, the button
    // did not change, and closing it left nothing to say what had happened.
    if (IconButton("##settings", Icon::Settings, "Settings", s, dpi, ImGui::IsPopupOpen("Settings")))
        ImGui::OpenPopup("Settings");

    // A position set here is taken as given, so keeping the panel on the
    // screen is this code's job. Mini sits at the bottom edge beside a game,
    // and the panel, its own window there, opened off the end of the monitor.
    for (const auto& monitor : ImGui::GetPlatformIO().Monitors) {
        const ImVec2 min = monitor.WorkPos, max(monitor.WorkPos.x + monitor.WorkSize.x, monitor.WorkPos.y + monitor.WorkSize.y);
        if (popupPosition.x < min.x - 344 * dpi || popupPosition.x >= max.x || popupPosition.y < min.y || popupPosition.y >= max.y) continue;
        popupMaxHeight = std::min(popupMaxHeight, max.y - min.y);
        popupPosition.y = std::max(min.y, std::min(popupPosition.y, max.y - popupMaxHeight));
        popupPosition.x = std::clamp(popupPosition.x, min.x, std::max(min.x, max.x - 344 * dpi));
        break;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(344 * dpi, 0), ImVec2(344 * dpi, popupMaxHeight));
    ImGui::SetNextWindowPos(popupPosition);
    if (ImGui::BeginPopup("Settings")) {
        DrawSettings(fonts, design, dpi, engine);
        ImGui::EndPopup();
    } else if (measuring_) {
        input_latency::stop();
        measuring_ = false;
        timingSummary_ = {};
    }
}

// The Convert audio popover. It used to be five buttons of equal weight in a
// bare popup: a file picker, a link button, a sign-in, a cancel, and a
// checkbox with a sentence beside it. Now there is one primary action, at
// the end of the field it acts on, and the same button becomes Cancel while
// the run it started is going, so the thing you press is always in one place.
// The file picker is the quieter second source, and sign-in is a footnote
// row, because it is done once and then forgotten. Errors use the bad colour
// rather than the accent, which the house rules keep for selection.
void Panels::DrawConvert(HWND hwnd, const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    const auto state = engine.Snapshot();
    const auto s = skin::ScaleGeometry(design, dpi);
    FontScope font(fonts, design, design.type.body * SpecFontScale(design));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(s.spacing.s2, s.spacing.s2));
    const float width = ImGui::GetContentRegionAvail().x;
    { FontScope title(fonts, design, design.type.heading * SpecFontScale(design), Weight::Semibold);
      ImGui::TextUnformatted("Convert audio to MIDI"); }

    const bool busy = state->converting;   // true while the sign-in window is open too
    const bool ready = !state->folder.empty();
    const bool playlistLink = audio_to_midi::IsPlaylistLink(convertLink_);
    // Sized for the widest label it carries, so the field beside it does not
    // jump when Convert becomes Cancel.
    const float actionWidth = ImGui::CalcTextSize("Convert").x + 2 * 16 * dpi;
    ImGui::BeginDisabled(busy || !ready);
    ImGui::SetNextItemWidth(width - actionWidth - s.spacing.s2);
    ImGui::InputTextWithHint("##convert-link", "Paste a YouTube or audio link", convertLink_, sizeof(convertLink_));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (busy) {
        if (ImGui::Button(state->signingIn ? "Close" : "Cancel", ImVec2(actionWidth, s.metric.controlHeight)))
            engine.Send({ShellEngine::Action::ConvertCancel});
    } else {
        ImGui::BeginDisabled(!ready || !convertLink_[0]);
        if (ImGui::Button("Convert", ImVec2(actionWidth, s.metric.controlHeight)))
            engine.Send({ShellEngine::Action::ConvertAudio, {}, 0, 0, playlistLink && convertPlaylist_, 0, convertLink_});
        ImGui::EndDisabled();
    }
    ImGui::BeginDisabled(busy || !ready);
    // Only for a link that names a playlist. Unticked, a video watched inside
    // a playlist still converts on its own.
    if (playlistLink) {
        SettingCheck("Whole playlist", convertPlaylist_, nullptr, fonts, design, dpi);
    }
    if (TransportButton("##convert-file", Icon::Open, "Choose an audio file", s, dpi)) {
        const auto path = PickFile(hwnd, PickKind::Audio);
        if (!path.empty()) engine.Send({ShellEngine::Action::ConvertAudio, path});
    }
    ImGui::EndDisabled();

    // Sign-in is a footnote: done once, then it only says which state it is in.
    ImGui::Separator();
    {
        FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
        const char* account = state->youtubeSignedIn ? "Signed in to YouTube" : "Not signed in to YouTube";
        const char* action = state->youtubeSignedIn ? "Sign in again" : "Sign in";
        const float actionText = ImGui::CalcTextSize(action).x + 2 * 8 * dpi;
        ImGui::PushStyleColor(ImGuiCol_Text, Colour(s.ink.secondary));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(account);
        ImGui::PopStyleColor();
        ImGui::SameLine(width - actionText);
        ImGui::BeginDisabled(busy);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * dpi, (s.metric.innerHeight - ImGui::GetTextLineHeight()) / 2));
        if (ImGui::Button(action, ImVec2(actionText, s.metric.innerHeight))) engine.Send({ShellEngine::Action::YouTubeSignIn});
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
    }

    // Progress: an indeterminate bar while a run is going, then the last line
    // the converter said, worded as a failure when it was one.
    if (busy && !state->signingIn) {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float height = 4 * dpi;
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, ImVec2(min.x + width, min.y + height), Colour(s.surface.recessed), height / 2);
        const float span = width * .3f;
        const float travel = static_cast<float>(std::fmod(ImGui::GetTime() * .6, 1.0)) * (width + span) - span;
        draw->AddRectFilled(ImVec2(min.x + std::max(0.f, travel), min.y),
                            ImVec2(min.x + std::min(width, travel + span), min.y + height), Colour(s.accent.accent), height / 2);
        ImGui::Dummy(ImVec2(width, height));
    }
    if (!state->conversionStatus.empty()) {
        const auto line = (state->conversionFailed ? "Failed: " : "") + state->conversionStatus;
        ImGui::PushStyleColor(ImGuiCol_Text, Colour(state->conversionFailed ? s.accent.bad : s.ink.primary));
        ImGui::PushTextWrapPos(width);
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar(2);
}

void Panels::DrawStatus(const Fonts& fonts, const skin::Skin& design, float dpi, const EngineSnapshot& state,
                       ImVec2 min, float width, float height) {
    const auto s = skin::ScaleGeometry(design, dpi);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, ImVec2(min.x + width, min.y + height), Colour(s.surface.structure));
    draw->AddLine(min, ImVec2(min.x + width, min.y), Colour(s.border.hairline), dpi);
    draw->PushClipRect(ImVec2(min.x, min.y + dpi), ImVec2(min.x + width, min.y + height), true);
    FontScope font(fonts, design, design.type.meta * SpecFontScale(design));
    const ImVec2 text(min.x + s.spacing.windowPad, min.y + (height - ImGui::GetTextLineHeight()) / 2);
    std::vector<std::string> fields;
    if (!state.error.empty()) fields.push_back(state.error);
    else if (state.busy) fields.push_back("Loading...");
    else {
        // The sheet's result leads while there is one; mini has no Export.
        if (!miniMode && !sheetNote_.empty()) fields.push_back(sheetNote_);
        fields.push_back(state.playing ? "Playing" : state.midiConnect ? "MidiConnect" : state.liveActive ? "Live" : "Ready");
        // A conversion outlives its popup, so the bar says one is running.
        if (state.converting) fields.push_back(state.signingIn ? "Signing in to YouTube" : "Converting audio");
        fields.push_back("Curve " + state.ActiveVelocityName());
        std::string input = "Input ";
        input += state.liveDevice.empty() ? "None" : BackendName(BackendForDeviceId(state.liveDevice));
        if (!state.liveDevice.empty() && !state.liveActive) input += " (off)";
        fields.push_back(input);
        fields.push_back(state.outputMidi ? "Output MIDI" : "Output Keystrokes");
    }
    // With nothing open there are no tracks to count, only the layout.
    const auto tracks = (state.rows.empty() ? std::string() :
        std::to_string(SilentTracks(state.rows)) + " of " + std::to_string(state.rows.size()) + " tracks silent \xc2\xb7 ") +
        (state.eightyEightKeys ? "88-key" : "61-key");
    // Sized from the label. A fixed 56dpi left the caller's frame padding to
    // clip "Log" inside the button in mini mode.
    const float logWidth = ImGui::CalcTextSize("Log").x + 24 * dpi;
    const float suffix = logWidth + (miniMode ? 0 : ImGui::CalcTextSize(tracks.c_str()).x + 24 * dpi);
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
    if (!miniMode) draw->AddText(ImVec2(min.x + width - s.spacing.windowPad - logWidth - 8 * dpi - ImGui::CalcTextSize(tracks.c_str()).x, text.y), Colour(s.ink.secondary), tracks.c_str());
    draw->PopClipRect();
    ImGui::SetCursorScreenPos(text);
    ImGui::InvisibleButton("##status", ImVec2(width - 2 * s.spacing.windowPad - logWidth, ImGui::GetTextLineHeight()));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", summary.c_str());
    ImGui::SetCursorScreenPos(ImVec2(min.x + width - s.spacing.windowPad - logWidth, min.y + 2 * dpi));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    if (ImGui::Button("Log", ImVec2(logWidth, height - 4 * dpi))) logOpen = !logOpen;
    ImGui::PopStyleVar();
}

void Panels::DrawLog(HWND hwnd, const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    if (!logOpen) return;
    const auto s = skin::ScaleGeometry(design, dpi);
    FontScope font(fonts, design, design.type.body * SpecFontScale(design));
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 limit(std::max(320 * dpi, viewport->WorkSize.x - 32 * dpi),
                       std::max(160 * dpi, viewport->WorkSize.y - 32 * dpi));
    ImGui::SetNextWindowSizeConstraints(ImVec2(320 * dpi, 160 * dpi), limit);
    ImGui::SetNextWindowSize(ImVec2(std::min(600 * dpi, limit.x), std::min(320 * dpi, limit.y)), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x / 2,
                                  viewport->WorkPos.y + viewport->WorkSize.y / 2), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    // No collapse arrow: ImGui's default title-bar triangle is the one
    // glyph in the app not drawn from the icon set, and folding the log to
    // a title bar is not a state anyone asked for.
    if (ImGui::Begin("Log", &logOpen, ImGuiWindowFlags_NoCollapse)) {
        const auto state = engine.Snapshot();
        if (IconButton("##clear-log", Icon::Clear, "Clear Log", s, dpi)) engine.Send({ShellEngine::Action::ClearLog});
        ImGui::SameLine();
        if (IconButton("##copy-log", Icon::Copy, "Copy Log", s, dpi)) CopyUtf8ToClipboard(hwnd, *state->log);
        ImGui::BeginChild("##log-output", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4 * dpi;
        if (!state->log->empty()) ImGui::TextUnformatted(state->log->data(), state->log->data() + state->log->size());
        if (atBottom) ImGui::SetScrollHereY(1.f);
        ImGui::EndChild();
    }
    ImGui::End();
}

// Keycaps, not a run of text. "F1 Play/Pause   F2 -10s   F3 +10s   F4 Stop"
// read as one sentence in the meta face; a bordered cap round each key
// separates the key from what it does, and a wider gap separates the pairs.
// A key another program holds is drawn as a dead key, flat and in the
// faintest ink, rather than labelled: the words pushed the song title out of
// its own row. `labels` off leaves the caps alone, for when the title needs
// the width.
float Panels::DrawTransportHints(ImDrawList* draw, const skin::Skin& s, float dpi, int seekStep, ImVec2 origin, bool labels) const {
    const auto back = "-" + std::to_string(seekStep) + "s", forward = "+" + std::to_string(seekStep) + "s";
    const std::string actions[]{"Play/Pause", back, forward, "Stop"};
    const float line = ImGui::GetTextLineHeight(), capPad = 5 * dpi, capHeight = line + 2 * dpi;
    const float pairGap = labels ? s.spacing.s4 : s.spacing.s2;
    float x = origin.x;
    for (size_t i = 0; i < transportKeys.size(); ++i) {
        const float capWidth = ImGui::CalcTextSize(transportKeys[i].c_str()).x + 2 * capPad;
        const bool available = transportKeysAvailable[i];
        if (draw) {
            const ImVec2 min(x, origin.y - dpi), max(x + capWidth, origin.y - dpi + capHeight);
            if (available) draw->AddRectFilled(min, max, Colour(s.surface.elevated), 4 * dpi);
            draw->AddRect(min, max, Colour(s.border.hairline), 4 * dpi, 0, dpi);
            draw->AddText(ImVec2(x + capPad, origin.y), Colour(available ? s.ink.primary : s.ink.tertiary), transportKeys[i].c_str());
            if (labels) draw->AddText(ImVec2(x + capWidth + s.spacing.s1, origin.y),
                                      Colour(available ? s.ink.secondary : s.ink.tertiary), actions[i].c_str());
        }
        x += capWidth + pairGap;
        if (labels) x += s.spacing.s1 + ImGui::CalcTextSize(actions[i].c_str()).x;
    }
    return x - origin.x - pairGap;
}

void Panels::DrawMini(HWND hwnd, const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine,
                     ImVec2 origin, ImVec2 size) {
    const auto state = engine.Snapshot();
    const auto s = skin::ScaleGeometry(design, dpi);
    FontScope font(fonts, design, design.type.body * SpecFontScale(design));
    const float control = s.metric.controlHeight, pad = s.spacing.windowPad;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (control - ImGui::GetTextLineHeight()) / 2));
    // Two rows -- device pill, then state pills -- with equal air above, between
    // and below, and the same 8dpi every other gap in mini uses.
    const float gap = 8 * dpi, stripPad = gap;
    const float strip = 3 * stripPad + 2 * control, status = 28 * dpi;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + strip), Colour(s.surface.structure));
    draw->AddLine(ImVec2(origin.x, origin.y + strip), ImVec2(origin.x + size.x, origin.y + strip), Colour(s.border.hairline));
    // Three utility slots. Key Mapping is a panel of the full window; its
    // button sat here permanently disabled.
    const float utilityX = origin.x + size.x - pad - 3 * control - 2 * s.spacing.s2;
    const float segmentX = utilityX - 172 * dpi;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + stripPad));
    { FontScope deviceFont(fonts, design, design.type.body * SpecFontScale(design), Weight::Medium);
      if (DevicePill(DeviceName(*state), std::max(40 * dpi, segmentX - s.spacing.s3 - origin.x - pad), s, dpi)) ImGui::OpenPopup("Settings"); }
    ImGui::SetCursorScreenPos(ImVec2(segmentX, origin.y + stripPad));
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
                draw->AddRect(a, b, Colour(s.accent.accent), s.radius.element, 0, dpi);
            }
        }
        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos(ImVec2(well.x + segmentWidth + 8 * dpi, well.y));
    }
    ImGui::SetCursorScreenPos(ImVec2(utilityX, origin.y + stripPad));
    if (IconButton("##restore-full", Icon::Expand, "Full window", s, dpi)) miniMode = false;
    ImGui::SameLine();
    if (IconButton("##mini-theme", s.dark ? Icon::Moon : Icon::Sun, s.dark ? "Switch to light" : "Switch to dark", s, dpi)) preferences.skin ^= 1;
    ImGui::SameLine();
    SettingsControl(fonts, design, dpi, engine,
                    ImVec2(origin.x + size.x - 344 * dpi - s.spacing.windowPad, origin.y + stripPad + control + 4 * dpi), 544 * dpi);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + 2 * stripPad + control));
    if (StatePills(fonts, design, dpi, engine, true)) autoVolumeOpen = true;
    const float row = origin.y + strip + gap;
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
            const auto path = PickMidiFile(hwnd);
            if (!path.empty()) engine.Send({ShellEngine::Action::Load, path, 0, 0, preferences.autoSolo});
        }
        ImGui::SameLine(); ImGui::BeginDisabled(state->rows.empty());
        { const bool applied = SoloPianoApplied(state->rows);
          if (IconButton("##mini-solo-piano", Icon::Piano, applied ? "Unmute all" : "Solo Piano", s, dpi, applied))
              engine.Send({applied ? ShellEngine::Action::UnmuteAll : ShellEngine::Action::SoloPiano, {}, state->generation}); }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(state->loaded.empty() || state->busy);
        const float seekHeight = 22 * dpi, transportY = row + control + 2 * gap + seekHeight;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, row + control + gap));
        if (!seeking_ || seekGeneration_ != state->generation) { seekPosition_ = static_cast<float>(state->position); seeking_ = false; }
        const bool changed = Groove("##mini-seek", &seekPosition_, 0, static_cast<float>(std::max(.001, state->duration)),
            size.x - 2 * pad, seekHeight, s, dpi, false);
        if (ImGui::IsItemActivated()) { seeking_ = true; seekGeneration_ = state->generation; }
        if (seeking_ && ImGui::IsItemDeactivatedAfterEdit()) { number(ShellEngine::Action::Seek, seekPosition_); seeking_ = false; }
        else if (changed && !ImGui::IsItemActive()) number(ShellEngine::Action::Seek, seekPosition_);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, transportY));
        if (PlayButton("##mini-play", state->playing, state->playbackCountdown, s, dpi))
            engine.Send({ShellEngine::Action::PlayCountdown, {}, state->generation});
        ImGui::SameLine();
        if (IconButton("##mini-restart", Icon::Refresh, "Restart", s, dpi)) engine.Send({ShellEngine::Action::Restart, {}, state->generation});
        ImGui::SameLine();
        // Labelled, as in the full window. The icons saved width on a row that
        // has width to spare, at the cost of making two seek buttons read as
        // two different controls between the layouts.
        if (TransportButton("##mini-back10", SeekLabel(state->seekStep, false).c_str(), s, dpi)) engine.Send({ShellEngine::Action::Back10, {}, state->generation});
        ImGui::SameLine();
        if (TransportButton("##mini-forward10", SeekLabel(state->seekStep, true).c_str(), s, dpi)) engine.Send({ShellEngine::Action::Forward10, {}, state->generation});
        ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::BeginDisabled(state->files->empty());
        if (IconButton("##mini-prev", Icon::Left, "Previous MIDI file", s, dpi)) engine.Send({ShellEngine::Action::Previous, {}, state->generation});
        ImGui::SameLine();
        if (IconButton("##mini-next", Icon::Right, "Next MIDI file", s, dpi)) engine.Send({ShellEngine::Action::Next, {}, state->generation});
        ImGui::EndDisabled(); ImGui::SameLine();
        if (IconButton("##mini-stop", Icon::Close, "Stop all output and cancel countdown", s, dpi)) engine.Send({ShellEngine::Action::Stop});
        const auto time = state->playbackCountdown ? "Starts in " + std::to_string(state->playbackCountdown) + "s" :
            Time(seeking_ ? seekPosition_ : state->position) + " / " + Time(state->duration);
        draw->AddText(ImVec2(origin.x + size.x - pad - ImGui::CalcTextSize(time.c_str()).x,
            transportY + (control - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.secondary), time.c_str());
        { FontScope meta(fonts, design, design.type.meta * SpecFontScale(design));
          DrawTransportHints(draw, s, dpi, state->seekStep, ImVec2(origin.x + pad, transportY + control + gap)); }
    }
    DrawStatus(fonts, design, dpi, *state, ImVec2(origin.x, origin.y + size.y - status), size.x, status);
    ImGui::PopStyleVar();
}

void Panels::Draw(HWND hwnd, const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    const auto s = skin::ScaleGeometry(design, dpi);
    auto state = engine.Snapshot();
    fileSort_ = state->fileSort;
    descendingFiles_ = state->descendingFiles;
    if (state->sheetReady && state->sheetRevision != handledSheetRevision_) {
        handledSheetRevision_ = state->sheetRevision;
        sheetStatusGeneration_ = state->generation;
        sheetPending_ = false;
        if (!state->sheetSaved.empty()) {
            // The page is the engine's; opening it is the panel's, because
            // a test engine must never launch a browser.
            const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", state->sheetSaved.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            sheetStatus_ = opened > 32 ? "Opened the sheet editor in your browser."
                                       : "Could not open a browser. The page is at " + Utf8(state->sheetSaved) + ".";
        }
        else if (!state->sheetFilesSaved.empty()) sheetStatus_ = "Saved the sheet in " + Utf8(state->sheetFilesSaved) + ".";
        else if (state->sheetText->empty() || state->sheetNotes == 0) sheetStatus_ = "No mapped notes to copy.";
        else if (CopyUtf8ToClipboard(hwnd, *state->sheetText))
            sheetStatus_ = "Copied " + std::to_string(state->sheetNotes) + " notes.";
        else sheetStatus_ = "Clipboard is busy. Try again.";
    }
    if (sheetStatusGeneration_ != state->generation) { sheetStatus_.clear(); sheetPending_ = false; }
    // A save that failed, a read-only folder for one, raises the engine's
    // error instead of a sheet; the status bar shows that, so the
    // "Writing..." line must not stay up waiting for a sheet that never comes.
    else if (sheetPending_ && !state->error.empty()) { sheetStatus_.clear(); sheetPending_ = false; }
    else if (!state->sheetReady && !sheetPending_) sheetStatus_.clear();
    if (!scannedLive_ && hwnd) { engine.Send({ShellEngine::Action::LiveScan}); scannedLive_ = true; }
    if (!scannedOutput_ && hwnd) { engine.Send({ShellEngine::Action::OutputScan}); scannedOutput_ = true; }
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
    if (miniMode) {
        DrawMini(hwnd, fonts, design, dpi, engine, origin, size);
        DrawAutoVolume(fonts, design, dpi, engine);
        DrawLog(hwnd, fonts, design, dpi, engine);
        mappingArmed_ = false;
        return;
    }
    auto* dl = ImGui::GetWindowDrawList();
    // One row: the state pills, then the device pill in what is left before
    // the utility buttons. The device had a row of its own, which was a second
    // 44dpi of strip for one pill, taken out of the Tracks panel.
    const float stripPad = 12 * dpi;
    const float strip = 2 * stripPad + s.metric.controlHeight, status = 28 * dpi;
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + strip), Colour(s.surface.structure));
    dl->AddLine(ImVec2(origin.x, origin.y + strip), ImVec2(origin.x + size.x, origin.y + strip), Colour(s.border.hairline));
    const float utilityX = origin.x + size.x - s.spacing.windowPad - 4 * s.metric.controlHeight - 3 * s.spacing.s2;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + s.spacing.windowPad, origin.y + stripPad));
    if (StatePills(fonts, design, dpi, engine, false)) autoVolumeOpen = true;
    ImGui::SameLine(0, s.spacing.s3);
    // Against the utility buttons, beside the Settings it opens, so its width
    // moves nothing else.
    { FontScope font(fonts, design, design.type.body * SpecFontScale(design), Weight::Medium);
      const auto name = DeviceName(*state);
      const float room = std::max(40 * dpi, utilityX - s.spacing.s3 - ImGui::GetCursorScreenPos().x);
      const float width = std::min(room, ImGui::CalcTextSize(name.c_str()).x + 26 * dpi);
      ImGui::SetCursorScreenPos(ImVec2(utilityX - s.spacing.s3 - width, origin.y + stripPad));
      if (DevicePill(name, room, s, dpi)) ImGui::OpenPopup("Settings"); }
    ImGui::SetCursorScreenPos(ImVec2(utilityX, origin.y + stripPad));
    if (IconButton("##mini-mode", Icon::Mini, "Mini mode", s, dpi)) miniMode = true;
    ImGui::SameLine();
    if (IconButton("##key-mapping", Icon::Keyboard, "Key Mapping", s, dpi, preferences.keyMappingOpen))
        preferences.keyMappingOpen = !preferences.keyMappingOpen;
    ImGui::SameLine();
    if (IconButton("##theme", s.dark ? Icon::Moon : Icon::Sun, s.dark ? "Switch to light" : "Switch to dark", s, dpi))
        preferences.skin ^= 1;
    ImGui::SameLine();
    SettingsControl(fonts, design, dpi, engine,
                    ImVec2(origin.x + size.x - 344 * dpi - s.spacing.windowPad, origin.y + 48 * dpi), size.y - 52 * dpi);

    const float top = origin.y + strip + s.spacing.windowPad;
    const float bottom = origin.y + size.y - status - s.spacing.windowPad;
    // The right column takes its 600 first: under that the velocity row's
    // sustain value ran off the panel. Files has what is left, 240 to 336.
    const float leftWidth = std::clamp(size.x - 2 * s.spacing.windowPad - s.spacing.s3 - 600 * dpi, 240 * dpi, 336 * dpi);
    const ImVec2 leftMin(origin.x + s.spacing.windowPad, top);
    const ImVec2 leftMax(leftMin.x + leftWidth, bottom);
    BeginPanel("Files", leftMin, leftMax, s);
    ImGui::PushFont(fonts.Get(design), design.type.body * SpecFontScale(design));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    { FontScope font(fonts, design, design.type.body * SpecFontScale(design), Weight::Semibold);
      ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("MIDI Files"); }
    ImGui::SameLine(ImGui::GetWindowWidth() - 2 * s.metric.controlHeight - s.spacing.s2);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * dpi, ImGui::GetStyle().FramePadding.y));
    if (IconButton("##sort-files", descendingFiles_ ? Icon::SortUp : Icon::SortDown, "Sort files", s, dpi))
        ImGui::OpenPopup("File sort");
    if (ImGui::BeginPopup("File sort")) {
        const char* labels[]{"Name", "Size", "Date modified"};
        for (int i = 0; i < 3; ++i) {
            if (ImGui::MenuItem(labels[i], nullptr, fileSort_ == static_cast<FileSort>(i))) {
                engine.Send({ShellEngine::Action::SortFiles, {}, 0, 0, descendingFiles_, static_cast<double>(i)});
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Ascending", nullptr, !descendingFiles_))
            engine.Send({ShellEngine::Action::SortFiles, {}, 0, 0, false, static_cast<double>(fileSort_)});
        if (ImGui::MenuItem("Descending", nullptr, descendingFiles_))
            engine.Send({ShellEngine::Action::SortFiles, {}, 0, 0, true, static_cast<double>(fileSort_)});
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state->playing || state->busy || state->folder.empty());
    if (IconButton("##refresh-files", Icon::Refresh, "Refresh MIDI files", s, dpi)) engine.Send({ShellEngine::Action::Scan, state->folder});
    ImGui::EndDisabled(); ImGui::PopStyleVar();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - s.metric.controlHeight - s.spacing.s2);
    ImGui::InputTextWithHint("##search", "Search MIDI files", search_, sizeof(search_));
    ImGui::SameLine();
    // One button for the three ways a file arrives. Three separate icon
    // buttons beside the search box, under two more in the header, was too
    // many controls for one small panel.
    // Marked while a conversion runs, because the popup can be closed over
    // it and this button is where the run is found again.
    if (IconButton("##add-files", Icon::Plus, "Add MIDI files", s, dpi, state->converting))
        ImGui::OpenPopup("Add MIDI files");
    bool convertRequested = openConvert;
    openConvert = false;
    if (ImGui::BeginPopup("Add MIDI files")) {
        if (ImGui::MenuItem("Open MIDI file...")) load(PickMidiFile(hwnd));
        if (ImGui::MenuItem("Choose MIDI folder...", nullptr, false, !state->playing && !state->busy)) {
            const auto path = PickFolder(hwnd);
            if (!path.empty()) { preferences.folder = path; engine.Send({ShellEngine::Action::Scan, path}); }
        }
        if (ImGui::MenuItem("Convert audio to MIDI...")) convertRequested = true;
        ImGui::EndPopup();
    }
    // Opened out here, where the menu was, rather than from inside the menu,
    // so it is a sibling of the menu and not a child that closes with it.
    if (convertRequested) ImGui::OpenPopup("Convert audio");
    ImGui::SetNextWindowSizeConstraints(ImVec2(360 * dpi, 0), ImVec2(360 * dpi, 10000 * dpi));
    if (ImGui::BeginPopup("Convert audio")) {
        DrawConvert(hwnd, fonts, design, dpi, engine);
        ImGui::EndPopup();
    }
    const ImVec2 listMin = ImGui::GetCursorScreenPos();
    const ImVec2 listSize = ImGui::GetContentRegionAvail();
    // The shadow is drawn after the rows, by RoundCorners below.
    skin::RecessedField(listMin, ImVec2(listMin.x + listSize.x, listMin.y + listSize.y), s, false);
    ImGui::BeginChild("##file-list", listSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (state->files->empty()) {
        // An empty list offers the thing that fills it, centred in the well.
        // It used to describe that in a sentence and leave the doing to a
        // menu behind the plus button.
        static constexpr const char* kChoose = "Choose MIDI folder";
        const ImVec2 area = ImGui::GetContentRegionAvail();
        const float width = 2 * 12 * dpi + 16 * dpi + s.spacing.s2 + ImGui::CalcTextSize(kChoose).x;
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + std::max(0.f, (area.x - width) / 2),
                                   ImGui::GetCursorPosY() + std::max(0.f, (area.y - s.metric.controlHeight) / 2)));
        ImGui::BeginDisabled(state->playing || state->busy);
        if (TransportButton("##choose-folder", Icon::Open, kChoose, s, dpi)) {
            const auto path = PickFolder(hwnd);
            if (!path.empty()) { preferences.folder = path; engine.Send({ShellEngine::Action::Scan, path}); }
        }
        ImGui::EndDisabled();
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
                return FileBefore((*state->files)[a], (*state->files)[b], fileSort_, descendingFiles_);
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
    // The list's own draw list, after EndChild has drawn its scrollbar, so the
    // corners cover a square selected row and the scrollbar alike.
    ImDrawList* fileListDraw = ImGui::GetWindowDrawList();
    ImGui::EndChild();
    skin::RoundCorners(fileListDraw, listMin, ImVec2(listMin.x + listSize.x, listMin.y + listSize.y),
                       s.radius.element, Colour(s.surface.card), s, true);
    ImGui::PopStyleVar(); ImGui::PopFont();
    ImGui::EndChild();

    const float right = leftMax.x + s.spacing.s3;
    const float edge = origin.x + size.x - s.spacing.windowPad;
    // The file name and the sheet action sit above the seek groove, and the
    // rows are 8dpi apart. The F-key hints sit on the title row, right of the
    // name: a line of their own cost the Tracks panel a row at the smallest
    // window. So did the line kept empty here for the sheet's result, which
    // the status bar now reports.
    const float titleHeight = s.metric.controlHeight;
    const float rowGap = 8 * dpi, seekHeight = 22 * dpi;
    const float playbackHeight = 2 * s.spacing.panelPad + titleHeight + seekHeight + 3 * rowGap + 2 * s.metric.controlHeight;
    BeginPanel("Playback", ImVec2(right, top), ImVec2(edge, top + playbackHeight), s,
               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushFont(fonts.Get(design), design.type.body * SpecFontScale(design));
    const auto content = ImGui::GetCursorScreenPos();
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const char* sheetLabel = "Export";
    const float sheetButtonWidth = 2 * 12 * dpi + 16 * dpi + s.spacing.s2 + ImGui::CalcTextSize(sheetLabel).x;
    // The title is the song and the legend is the same four keys every day,
    // so the legend gives way: whole, then caps only, then not at all.
    const std::string title = state->loaded.empty() ? "Playback" : Utf8(state->loaded.stem());
    float titleWidth = 0;
    { FontScope font(fonts, design, 20 * SpecFontScale(design), Weight::Medium);
      titleWidth = ImGui::CalcTextSize(title.c_str()).x; }
    float hintsWidth = 0;
    { FontScope font(fonts, design, design.type.meta * SpecFontScale(design));
      const float room = contentWidth - sheetButtonWidth - s.spacing.s2 - titleWidth - s.spacing.s3;
      const float capsWidth = DrawTransportHints(nullptr, s, dpi, state->seekStep, {}, false);
      const bool labels = DrawTransportHints(nullptr, s, dpi, state->seekStep, {}, true) <= room;
      if (labels || capsWidth <= std::max(room, contentWidth * .25f)) {
          hintsWidth = (labels ? DrawTransportHints(nullptr, s, dpi, state->seekStep, {}, true) : capsWidth) + s.spacing.s3;
          DrawTransportHints(ImGui::GetWindowDrawList(), s, dpi, state->seekStep,
              ImVec2(content.x + contentWidth - sheetButtonWidth - hintsWidth + s.spacing.s3 - s.spacing.s2,
                     content.y + (titleHeight - ImGui::GetTextLineHeight()) / 2), labels);
      } }
    { FontScope font(fonts, design, 20 * SpecFontScale(design), Weight::Medium);
      DrawEllipsis(title,
                   contentWidth - sheetButtonWidth - hintsWidth - s.spacing.s2, ImVec2(content.x, content.y + (titleHeight - ImGui::GetTextLineHeight()) / 2)); }
    ImGui::SetCursorScreenPos(ImVec2(content.x + contentWidth - sheetButtonWidth, content.y));
    const bool haveFile = !state->loaded.empty() && !state->rows.empty();
    ImGui::BeginDisabled(state->busy || (!haveFile && state->files->empty()));
    if (TransportButton("##export", Icon::Down, sheetLabel, s, dpi)) ImGui::OpenPopup("Export MIDI");
    if (ImGui::BeginPopup("Export MIDI")) {
        const auto request = [&](ShellEngine::Action action, const char* status) {
            send(action);
            sheetStatusGeneration_ = state->generation;
            sheetStatus_ = status;
            sheetPending_ = true;
        };
        // Copy sheet is the quick text for a chat. The editor is
        // midi-converter's page in the browser: the coloured sheet with every
        // setting beside it, redrawn as they change, its own Copy, Save and
        // Print. All customising happens there, where the result is visible;
        // the app keeps no sheet settings of its own. Files are the same
        // sheet written where it can be kept: under the sheets folder, in the
        // MIDI folder's own sub-folders, styled by a page saved from the
        // editor, for the open file or the whole list.
        // Every item says what it does in its label; a menu of "Copy sheet /
        // Image / Text" meant nothing to a first user, and tooltips are not
        // the fix for that.
        ImGui::BeginDisabled(!haveFile);
        if (ImGui::MenuItem("Copy sheet to clipboard")) request(ShellEngine::Action::CopySheet, "Preparing sheet...");
        if (ImGui::MenuItem("Open sheet editor in browser")) request(ShellEngine::Action::OpenSheetEditor, "Opening the sheet editor...");
        if (ImGui::MenuItem("Save sheet files for this MIDI")) request(ShellEngine::Action::SaveSheetFiles, "Saving sheet files...");
        ImGui::EndDisabled();
        if (state->sheetBatchRunning) {
            if (ImGui::MenuItem("Stop saving the library")) engine.Send({ShellEngine::Action::SheetBatchCancel});
        } else if (ImGui::MenuItem("Save sheet files for every MIDI in the list...", nullptr, false, !state->files->empty())) {
            openLibrarySave = true;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Files to save");
        const auto output = [&](const char* label, bool on, const char* key) {
            if (ImGui::MenuItem(label)) engine.Send({ShellEngine::Action::SheetFiles, {}, 0, 0, !on, 0, key});
            // The skin's tick. ImGui's own is a heavy black stroke beside a
            // window of Lucide marks.
            if (on) {
                const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                const float side = 16 * dpi;
                DrawIcon(ImGui::GetWindowDrawList(), Icon::Check, ImVec2(max.x - side - 2 * dpi, min.y + (max.y - min.y - side) / 2),
                         side, Colour(s.accent.accent), dpi);
            }
        };
        output("Image (.png)", state->sheetImage, "image");
        output("Text (.txt)", state->sheetTextFile, "text");
        output("Editor page (.html)", state->sheetPageFile, "page");
        ImGui::Separator();
        const auto sheetsFolder = state->sheetsFolder.empty() ? DefaultSheetsFolder(state->folder) : state->sheetsFolder;
        if (ImGui::MenuItem(("Save to: " + Utf8(sheetsFolder) + "...").c_str())) {
            const auto path = PickFolder(hwnd);
            if (!path.empty()) engine.Send({ShellEngine::Action::SheetsFolder, path});
        }
        const std::string styleLabel = state->sheetStylePage.empty() ? "Sheet style: editor defaults..."
                                                                       : "Sheet style: " + Utf8(state->sheetStylePage.filename()) + "...";
        if (ImGui::MenuItem(styleLabel.c_str())) {
            const auto path = PickFile(hwnd, PickKind::Page);
            if (!path.empty()) engine.Send({ShellEngine::Action::SheetStylePage, path});
        }
        if (!state->sheetStylePage.empty() && ImGui::MenuItem("Back to the editor's defaults")) engine.Send({ShellEngine::Action::SheetStylePage, {}});
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    // The library save writes files for every MIDI in the list, hundreds for
    // a real library, so a menu item alone must not start it. This says how
    // many, what and where, and only its own button begins. Opened here, a
    // sibling of the menu, since the menu closes on the click.
    if (openLibrarySave) { ImGui::OpenPopup("Save library sheets"); openLibrarySave = false; }
    ImGui::SetNextWindowSizeConstraints(ImVec2(440 * dpi, 0), ImVec2(440 * dpi, 10000 * dpi));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save library sheets", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)) {
        const size_t total = state->files->size();
        const auto count = std::to_string(total);
        std::vector<std::string> kinds;
        if (state->sheetImage) kinds.push_back("an image");
        if (state->sheetTextFile) kinds.push_back("a text file");
        if (state->sheetPageFile) kinds.push_back("an editor page");
        std::string what;
        for (size_t i = 0; i < kinds.size(); ++i)
            what += (i == 0 ? "" : i + 1 == kinds.size() ? " and " : ", ") + kinds[i];
        if (!what.empty()) what[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(what[0])));
        const auto sheetsFolder = state->sheetsFolder.empty() ? DefaultSheetsFolder(state->folder) : state->sheetsFolder;
        { FontScope font(fonts, design, design.type.body * SpecFontScale(design), Weight::Semibold);
          ImGui::TextUnformatted("Save sheet files for every MIDI in the list?"); }
        ImGui::Spacing();
        // What and how many, then where on its own line: "under" used to
        // wrap onto a line by itself. With nothing ticked the button is off
        // and says so by being off.
        if (!kinds.empty()) ImGui::TextWrapped("%s", (what + (total == 1 ? " for the one MIDI file" : " for each of the " + count + " MIDI files")).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, Colour(s.ink.secondary));
        ImGui::TextWrapped("%s", Utf8(sheetsFolder).c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::BeginDisabled(kinds.empty());
        if (TransportButton("##library-save-go", (total == 1 ? "Save 1 sheet" : "Save " + count + " sheets").c_str(), s, dpi, true)) {
            engine.Send({ShellEngine::Action::SaveLibrarySheets});
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (TransportButton("##library-save-cancel", "Cancel", s, dpi)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    sheetNote_ = sheetStatus_;
    if (sheetNote_.empty()) sheetNote_ = state->sheetBatchStatus;
    if (state->sheetReady && state->sheetMerged)
        sheetNote_ += " " + std::to_string(state->sheetMerged) + " shared notes merged.";
    if (state->sheetReady && state->sheetUnmapped)
        sheetNote_ += " " + std::to_string(state->sheetUnmapped) + " unmapped notes dropped.";
    const auto number = [&](ShellEngine::Action action, double amount) {
        engine.Send({action, {}, state->generation, 0, false, amount});
    };
    ImGui::BeginDisabled(state->loaded.empty() || state->rows.empty() || state->busy);
    ImGui::SetCursorScreenPos(ImVec2(content.x, content.y + titleHeight + rowGap));
    if (!seeking_ || seekGeneration_ != state->generation) {
        seekPosition_ = static_cast<float>(state->position);
        seeking_ = false;
    }
    const bool seekChanged = Groove("##seek", &seekPosition_, 0, static_cast<float>(std::max(.001, state->duration)),
                                     contentWidth, seekHeight, s, dpi, false);
    if (ImGui::IsItemActivated()) { seeking_ = true; seekGeneration_ = state->generation; }
    if (seeking_ && ImGui::IsItemDeactivatedAfterEdit()) {
        if (seekGeneration_ == state->generation) number(ShellEngine::Action::Seek, seekPosition_);
        seeking_ = false;
    } else if (seekChanged && !ImGui::IsItemActive()) number(ShellEngine::Action::Seek, seekPosition_);
    if (ImGui::IsItemHovered() || seeking_) ImGui::SetTooltip("%s", Time(seekPosition_).c_str());
    const float transportY = content.y + titleHeight + seekHeight + 2 * rowGap;
    ImGui::SetCursorScreenPos(ImVec2(content.x, transportY));
    if (PlayButton("##play", state->playing, state->playbackCountdown, s, dpi))
        send(ShellEngine::Action::PlayCountdown);
    ImGui::SameLine();
    if (IconButton("##restart", Icon::Refresh, "Restart", s, dpi)) send(ShellEngine::Action::Restart);
    ImGui::SameLine();
    if (TransportButton("##back10", SeekLabel(state->seekStep, false).c_str(), s, dpi)) send(ShellEngine::Action::Back10);
    ImGui::SameLine();
    if (TransportButton("##forward10", SeekLabel(state->seekStep, true).c_str(), s, dpi)) send(ShellEngine::Action::Forward10);
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(state->files->empty() || state->busy);
    if (IconButton("##previous", Icon::Left, "Previous MIDI file", s, dpi)) send(ShellEngine::Action::Previous);
    ImGui::SameLine();
    if (IconButton("##next", Icon::Right, "Next MIDI file", s, dpi)) send(ShellEngine::Action::Next);
    ImGui::EndDisabled(); ImGui::SameLine();
    if (IconButton("##stop", Icon::Close, "Stop all output and cancel countdown", s, dpi)) send(ShellEngine::Action::Stop);
    ImGui::BeginDisabled(state->loaded.empty() || state->rows.empty() || state->busy);
    const std::string time = state->playbackCountdown ? "Starts in " + std::to_string(state->playbackCountdown) + "s" :
        Time(seeking_ ? seekPosition_ : state->position) + " / " + Time(state->duration);
    dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(content.x + contentWidth - ImGui::CalcTextSize(time.c_str()).x,
                transportY + (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2), Colour(s.ink.secondary), time.c_str());
    ImGui::SetCursorScreenPos(ImVec2(content.x, transportY + s.metric.controlHeight + rowGap));
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
    const float requestedCurveHeight = velocityExpanded ?
        (428.f + (nameOperation_ ? 44.f : 0.f)) * dpi : collapsedHeight;
    const float curveHeight = std::min(requestedCurveHeight, std::max(collapsedHeight, bottom - trackTop - 168 * dpi));
    const float curveTop = bottom - curveHeight;
    BeginPanel("Tracks", ImVec2(right, trackTop), ImVec2(edge, curveTop - s.spacing.s3), s);
    ImGui::PushFont(fonts.Get(design), design.type.body * SpecFontScale(design));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * dpi, (s.metric.controlHeight - ImGui::GetTextLineHeight()) / 2));
    { FontScope font(fonts, design, design.type.body * SpecFontScale(design), Weight::Semibold); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Tracks"); }
    // One toggle. On means the rows are exactly what Solo Piano leaves; a
    // second click brings every track back. Unmute All was a second button
    // for the same pair of states, and the owner asked for one.
    const bool applied = SoloPianoApplied(state->rows);
    const bool allPiano = AllPiano(state->rows);
    const float actionsWidth = 2 * 12 * dpi + 16 * dpi + s.spacing.s2 + ImGui::CalcTextSize("Solo Piano").x;
    ImGui::SameLine(ImGui::GetWindowWidth() - actionsWidth);
    ImGui::BeginDisabled(state->rows.empty() || state->busy || allPiano);
    if (TransportButton("##solo-piano", Icon::Piano, "Solo Piano", s, dpi, false, applied))
        send(applied ? ShellEngine::Action::UnmuteAll : ShellEngine::Action::SoloPiano);
    if (applied && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Unmute all");
    ImGui::EndDisabled();
    const ImVec2 tableMin = ImGui::GetCursorScreenPos();
    const ImVec2 tableSize = ImGui::GetContentRegionAvail();
    // No recessed fill here. Rows are card-coloured, so the recessed grey only
    // showed as a sliver under the last row, with its rounded bottom corners
    // floating in it. The frame and its corners are drawn after the table.
    ImDrawList* tableDraw = nullptr;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(s.spacing.s2, s.spacing.s1));
    // Rules go above each row, drawn after the table. BordersInnerH put one
    // under the last row too, which left the space below it looking like a
    // clipped row.
    std::vector<float> rules;
    float headerBottom = tableMin.y, headerHeight = 0.f;
    const float rowHeight = s.metric.controlHeight + 2 * s.spacing.s1;
    // The two text columns share the width by what this file puts in them.
    // Fixed at 1.2 to 1, "Acoustic Grand Piano" was cut short beside a TRACK
    // column half empty. A weight is read once, when a table is created, so
    // the table is keyed by the load.
    float nameWeight = ImGui::CalcTextSize("TRACK").x, instrumentWeight = ImGui::CalcTextSize("INSTRUMENT").x;
    for (const auto& row : state->rows) {
        nameWeight = std::max(nameWeight, ImGui::CalcTextSize(row.name.c_str()).x * 1.06f);
        instrumentWeight = std::max(instrumentWeight, ImGui::CalcTextSize(row.instrument.c_str()).x +
            (row.piano ? 14 * dpi + s.spacing.s1 : 0.f));
    }
    const std::string tableId = "##tracks-" + std::to_string(state->generation);
    // PadOuterX, or the # column sits flush against the frame's left edge.
    if (ImGui::BeginTable(tableId.c_str(), 7, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX, tableSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 20 * dpi);
        ImGui::TableSetupColumn("TRACK", ImGuiTableColumnFlags_WidthStretch, nameWeight);
        ImGui::TableSetupColumn("INSTRUMENT", ImGuiTableColumnFlags_WidthStretch, instrumentWeight);
        ImGui::TableSetupColumn("CH", ImGuiTableColumnFlags_WidthFixed, 28 * dpi);
        ImGui::TableSetupColumn("NOTES", ImGuiTableColumnFlags_WidthFixed, 48 * dpi);
        ImGui::TableSetupColumn("##mute-heading", ImGuiTableColumnFlags_WidthFixed, s.metric.controlHeight + s.spacing.s2);
        ImGui::TableSetupColumn("##solo-heading", ImGuiTableColumnFlags_WidthFixed, s.metric.controlHeight + s.spacing.s2);
        std::array<ImVec2, 2> actionHeaderMin, actionHeaderMax;
        { FontScope font(fonts, design, design.type.meta * SpecFontScale(design), Weight::Semibold);
          // An explicit height in the heading font, so the header's bottom is
          // known exactly; TableGetHeaderRowHeight() measures in the body font.
          headerHeight = ImGui::GetTextLineHeight() + 2 * ImGui::GetStyle().CellPadding.y;
          ImGui::TableNextRow(ImGuiTableRowFlags_Headers, headerHeight);
          for (int column = 0; column < 7; ++column) {
              ImGui::TableSetColumnIndex(column);
              // Always the column's own name, never "". An empty label takes
              // its ID from the parent, so the two icon columns collided with
              // each other and ImGui said so on screen.
              // The mute and solo names begin with ## and so draw nothing;
              // their separate labels are painted over the two columns below.
              ImGui::TableHeader(ImGui::TableGetColumnName(column));
          } }
        const auto* table = ImGui::GetCurrentTable();
        // The real header row, not a guessed offset, keeps both labels on the
        // same baseline as the five headers beside them.
        for (int action = 0; action < 2; ++action) {
            actionHeaderMin[action] = ImVec2(table->Columns[5 + action].WorkMinX, table->RowPosY1);
            actionHeaderMax[action] = ImVec2(table->Columns[5 + action].WorkMinX + s.metric.controlHeight,
                table->RowPosY1 + headerHeight);
        }
        headerBottom = table->RowPosY1 + headerHeight;
        const bool anySolo = AnySolo(state->rows);
        ImGuiListClipper tracks;
        tracks.Begin(static_cast<int>(state->rows.size()), rowHeight);
        while (tracks.Step()) for (int rowIndex = tracks.DisplayStart; rowIndex < tracks.DisplayEnd; ++rowIndex) {
            const auto& row = state->rows[rowIndex];
            const bool audible = TrackAudible(row, anySolo);
            ImGui::PushID(static_cast<int>(row.index));
            ImGui::TableNextRow(0, rowHeight);
            rules.push_back(table->RowPosY1);
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
            ImGui::TableNextRow(0, rowHeight);
            rules.push_back(table->RowPosY1);
            ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding();
            // A file with nothing to play says so. No file says nothing: the
            // list beside this is where one is opened.
            if (!state->loaded.empty()) ImGui::TextUnformatted("No note tracks");
        }
        { FontScope font(fonts, design, design.type.meta * SpecFontScale(design), Weight::Semibold);
          auto* headers = ImGui::GetWindowDrawList();
          for (int action = 0; action < 2; ++action) {
              const char* label = action ? "SOLO" : "MUTE";
              const float textWidth = ImGui::CalcTextSize(label).x;
              headers->PushClipRect(actionHeaderMin[action], actionHeaderMax[action], false);
              // Same top padding and ink as TableHeader, so all seven headings
              // share one baseline and one colour.
              headers->AddText(ImVec2(actionHeaderMin[action].x +
                                          (actionHeaderMax[action].x - actionHeaderMin[action].x - textWidth) / 2,
                                      actionHeaderMin[action].y + ImGui::GetStyle().CellPadding.y),
                               ImGui::GetColorU32(ImGuiCol_Text), label);
              headers->PopClipRect();
          } }
        // The scrolling table draws into its own inner window, which renders
        // over this panel, so the frame has to go into that list, after it.
        tableDraw = ImGui::GetCurrentTable()->InnerWindow->DrawList;
        ImGui::EndTable();
    }
    if (tableDraw) {
        const float left = tableMin.x, right = tableMin.x + tableSize.x;
        tableDraw->AddLine(ImVec2(left, headerBottom), ImVec2(right, headerBottom), ImGui::GetColorU32(ImGuiCol_TableBorderStrong));
        // Clipped below the header, so a row scrolled under it cannot draw its
        // rule across the headings; the first row's rule is the header's own.
        tableDraw->PushClipRect(ImVec2(left, headerBottom + 1), ImVec2(right, tableMin.y + tableSize.y), false);
        for (const float y : rules)
            if (y > headerBottom + 0.5f)
                tableDraw->AddLine(ImVec2(left, y), ImVec2(right, y), ImGui::GetColorU32(ImGuiCol_TableBorderLight));
        tableDraw->PopClipRect();
        skin::RoundCorners(tableDraw, tableMin, ImVec2(tableMin.x + tableSize.x, tableMin.y + tableSize.y),
                           s.radius.element, Colour(s.surface.card), s, true);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleVar(); ImGui::PopFont();
    ImGui::EndChild();

    DrawVelocity(fonts, design, dpi, engine, ImVec2(right, curveTop), ImVec2(edge, bottom));
    DrawStatus(fonts, design, dpi, *state, ImVec2(origin.x, origin.y + size.y - status), size.x, status);
    if (preferences.keyMappingOpen) DrawKeyMapping(fonts, design, dpi, engine);
    else mappingArmed_ = false;
    DrawAutoVolume(fonts, design, dpi, engine);
    DrawLog(hwnd, fonts, design, dpi, engine);
}
}
