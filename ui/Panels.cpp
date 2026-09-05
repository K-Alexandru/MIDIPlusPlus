#include "Panels.hpp"
#include "json.hpp"
#include <commdlg.h>
#include <shobjidl.h>
#include <fstream>
#include <cmath>

namespace shell {
namespace {
ImU32 Colour(skin::Argb c) { return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, (c >> 24) & 255); }
enum class Icon { Folder, Open, Refresh, Settings, Sun, Moon, Play, Stop, Speaker, Muted, Solo, Piano };

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
    case Icon::Stop: rect(5, 5, 19, 19); break;
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

void BeginPanel(const char* id, ImVec2 min, ImVec2 max, const skin::Skin& s) {
    skin::RaisedPanel(min, max, s);
    ImGui::SetCursorScreenPos(ImVec2(min.x + s.spacing.panelPad, min.y + s.spacing.panelPad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild(id, ImVec2(max.x - min.x - 2 * s.spacing.panelPad,
                     max.y - min.y - 2 * s.spacing.panelPad), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
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
        const auto folder = json.value("midiFolder", std::string());
        preferences.folder = std::filesystem::path(std::u8string(folder.begin(), folder.end()));
    } catch (const std::exception&) { preferences = {}; }
}

void Panels::SavePreferences(const std::filesystem::path& path) const {
    nlohmann::json json{{"skin", preferences.skin}, {"autoSoloPiano", preferences.autoSolo},
                        {"midiFolder", Utf8(preferences.folder)}};
    std::ofstream stream(path);
    if (stream) stream << json.dump(2) << '\n';
}

void Panels::Draw(HWND hwnd, const Fonts& fonts, const skin::Skin& design, float dpi, ShellEngine& engine) {
    const auto s = skin::ScaleGeometry(design, dpi);
    auto state = engine.Snapshot();
    const auto send = [&](ShellEngine::Action action, size_t track = 0, bool value = false) {
        engine.Send({action, {}, state->generation, track, value});
    };
    const auto load = [&](const std::filesystem::path& path) {
        countdown_ = false;
        if (!path.empty()) engine.Send({ShellEngine::Action::Load, path, 0, 0, preferences.autoSolo});
    };
    if (countdown_ && (state->generation != playGeneration_ || state->busy)) countdown_ = false;
    if (countdown_ && std::chrono::steady_clock::now() >= playAt_) {
        countdown_ = false;
        send(ShellEngine::Action::Play);
    }

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

    ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - s.spacing.windowPad - 2 * s.metric.controlHeight - s.spacing.s2,
                                    origin.y + s.spacing.s2));
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
    const float playbackHeight = (design.type.family == "IBM Plex Sans" ? 156.f : 140.f) * dpi;
    BeginPanel("Playback", ImVec2(right, top), ImVec2(edge, top + playbackHeight), s);
    { FontScope font(fonts, design, design.type.heading, Weight::Semibold);
      Ellipsis(state->loaded.empty() ? "Playback" : Utf8(state->loaded.filename()), ImGui::GetContentRegionAvail().x); }
    const float fraction = state->duration > 0 ? static_cast<float>(state->position / state->duration) : 0;
    ImGui::ProgressBar(fraction, ImVec2(-1, s.spacing.s1), "");
    ImGui::BeginDisabled(state->loaded.empty() || state->rows.empty() || state->busy);
    if (state->playing || countdown_) {
        if (ImGui::Button(countdown_ ? "Cancel" : "Stop", ImVec2(96 * dpi, s.metric.controlHeight))) {
            countdown_ = false; send(ShellEngine::Action::Stop);
        }
    } else if (ImGui::Button("Play in 3s", ImVec2(96 * dpi, s.metric.controlHeight))) {
        countdown_ = true;
        playAt_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        playGeneration_ = state->generation;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const std::string time = Time(state->position) + " / " + Time(state->duration);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(time.c_str());
    { FontScope font(fonts, design, design.type.meta);
      if (countdown_) ImGui::TextUnformatted("Focus your piano game. Playback starts shortly.");
      else if (state->playing) ImGui::TextUnformatted(stopHotkeyAvailable ? "F4 stops playback." : "Sending keystrokes to the focused window.");
      else ImGui::TextDisabled("Playback sends keystrokes. Switch to your piano game during the countdown."); }
    ImGui::EndChild();

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
}
}
