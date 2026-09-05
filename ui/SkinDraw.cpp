#include "SkinDraw.hpp"

namespace skin {
namespace {

// Skin stores 0xAARRGGBB; ImGui packs 0xAABBGGRR.
ImU32 ToImU32(Argb c) {
    const uint32_t a = (c >> 24) & 0xFF, r = (c >> 16) & 0xFF,
                   g = (c >> 8) & 0xFF,  b = c & 0xFF;
    return IM_COL32(r, g, b, a);
}

ImVec4 ToVec4(Argb c) {
    return ImVec4(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f,
                  (c & 0xFF) / 255.f, ((c >> 24) & 0xFF) / 255.f);
}

// Scale an ImU32's alpha, for stacking shadow layers.
ImU32 Fade(ImU32 c, float factor) {
    const uint32_t a = static_cast<uint32_t>(((c >> IM_COL32_A_SHIFT) & 0xFF) * factor);
    return (c & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

// A blur approximated by concentric translucent rounded rects. Three layers is
// the point where adding more stops being visible at these radii.
void StackedShadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding,
                   const Shadow& sh) {
    const int layers = 3;
    const ImU32 base = ToImU32(sh.colour);
    for (int i = layers; i >= 1; --i) {
        const float spread = sh.blur * (static_cast<float>(i) / layers);
        dl->AddRectFilled(ImVec2(min.x - spread, min.y - spread + sh.offsetY),
                          ImVec2(max.x + spread, max.y + spread + sh.offsetY),
                          Fade(base, 1.f / layers), rounding + spread);
    }
}

} // namespace

void ApplyStyle(const Skin& s) {
    ImGuiStyle& st = ImGui::GetStyle();

    // Concentric radii. An element nested inside a container is drawn at the
    // container's radius minus its inset, which is what keeps a rounded box
    // from looking glued inside another rounded box.
    st.WindowRounding = s.radius.window;
    st.ChildRounding  = s.radius.card;
    st.FrameRounding  = s.radius.control;
    st.PopupRounding  = s.radius.card;
    st.GrabRounding   = s.radius.element;
    st.TabRounding    = s.radius.element;
    st.ScrollbarRounding = s.radius.element;

    // The 4px scale, and nothing off it.
    st.WindowPadding    = ImVec2(s.spacing.windowPad, s.spacing.windowPad);
    st.FramePadding     = ImVec2(s.spacing.s3, (s.metric.controlHeight - s.type.body) * 0.5f);
    st.ItemSpacing      = ImVec2(s.spacing.s2, s.spacing.s2);
    st.ItemInnerSpacing = ImVec2(s.spacing.s2, s.spacing.s1);
    st.CellPadding      = ImVec2(s.spacing.s3, s.spacing.s1);
    st.IndentSpacing    = s.spacing.s4;

    // Hairlines, never solid greys.
    st.WindowBorderSize = 1.f;
    st.ChildBorderSize  = 1.f;
    st.FrameBorderSize  = 1.f;
    st.PopupBorderSize  = 1.f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]        = ToVec4(s.surface.canvas);
    c[ImGuiCol_ChildBg]         = ToVec4(s.surface.card);
    c[ImGuiCol_PopupBg]         = ToVec4(s.surface.card);
    c[ImGuiCol_MenuBarBg]       = ToVec4(s.surface.structure);
    c[ImGuiCol_Border]          = ToVec4(s.border.hairline);
    c[ImGuiCol_BorderShadow]    = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Text]            = ToVec4(s.ink.primary);
    c[ImGuiCol_TextDisabled]    = ToVec4(s.ink.tertiary);

    // Frames are the recessed tier: this is what you type into or read from.
    c[ImGuiCol_FrameBg]         = ToVec4(s.surface.recessed);
    c[ImGuiCol_FrameBgHovered]  = ToVec4(s.surface.elevatedHot);
    c[ImGuiCol_FrameBgActive]   = ToVec4(s.surface.recessed);

    // Buttons are the elevated tier.
    c[ImGuiCol_Button]          = ToVec4(s.surface.elevated);
    c[ImGuiCol_ButtonHovered]   = ToVec4(s.surface.elevatedHot);
    c[ImGuiCol_ButtonActive]    = ToVec4(s.surface.recessed);
    c[ImGuiCol_Header]          = ToVec4(s.accent.accentSoft);
    c[ImGuiCol_HeaderHovered]   = ToVec4(s.surface.elevatedHot);
    c[ImGuiCol_HeaderActive]    = ToVec4(s.accent.accentSoft);

    // Accent is selection, focus and fills. Not decoration.
    c[ImGuiCol_CheckMark]       = ToVec4(s.accent.accent);
    c[ImGuiCol_SliderGrab]      = ToVec4(s.surface.elevated);
    c[ImGuiCol_SliderGrabActive]= ToVec4(s.surface.elevated);
    c[ImGuiCol_NavCursor]       = ToVec4(s.accent.accent);
    c[ImGuiCol_PlotLines]       = ToVec4(s.accent.accent);

    c[ImGuiCol_TitleBg]         = ToVec4(s.surface.structure);
    c[ImGuiCol_TitleBgActive]   = ToVec4(s.surface.structure);
    c[ImGuiCol_TitleBgCollapsed]= ToVec4(s.surface.structure);
    c[ImGuiCol_Separator]       = ToVec4(s.border.hairline);
    c[ImGuiCol_TableBorderLight]= ToVec4(s.border.hairline);
    c[ImGuiCol_TableBorderStrong]= ToVec4(s.border.strong);
    c[ImGuiCol_TableHeaderBg]   = ToVec4(s.surface.structure);
    c[ImGuiCol_TableRowBg]      = ToVec4(s.surface.card);
    c[ImGuiCol_TableRowBgAlt]   = ToVec4(s.surface.card);
    c[ImGuiCol_ScrollbarBg]     = ToVec4(s.surface.recessed);
    c[ImGuiCol_ScrollbarGrab]   = ToVec4(s.surface.elevated);
}

void RaisedRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding,
                const Skin& s, ImU32 fill, bool topHighlight) {
    StackedShadow(dl, min, max, rounding, s.ambient);
    StackedShadow(dl, min, max, rounding, s.contact);
    dl->AddRectFilled(min, max, fill, rounding);
    dl->AddRect(min, max, ToImU32(s.border.hairline), rounding);
    if (topHighlight) {
        // One pixel lighter, inside the top edge. Inset by the radius so the
        // line stops where the corner starts curving.
        dl->AddLine(ImVec2(min.x + rounding, min.y + 1.f),
                    ImVec2(max.x - rounding, min.y + 1.f),
                    ToImU32(s.border.topHighlight));
    }
}

void RecessedRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding,
                  const Skin& s) {
    dl->AddRectFilled(min, max, ToImU32(s.surface.recessed), rounding);
    // Inner shadow: a soft darker band inside the top edge, which is what makes
    // a groove read as a groove rather than as nothing.
    const ImU32 shade = ToImU32(s.inner.colour);
    for (int i = 0; i < 3; ++i) {
        dl->AddLine(ImVec2(min.x + rounding, min.y + 1.f + i),
                    ImVec2(max.x - rounding, min.y + 1.f + i),
                    Fade(shade, 1.f - i * 0.3f));
    }
    dl->AddRect(min, max, ToImU32(s.border.hairline), rounding);
}

void RaisedPanel(ImVec2 min, ImVec2 max, const Skin& s) {
    RaisedRect(ImGui::GetWindowDrawList(), min, max, s.radius.card, s,
               ToImU32(s.surface.card));
}

void RecessedField(ImVec2 min, ImVec2 max, const Skin& s) {
    RecessedRect(ImGui::GetWindowDrawList(), min, max, s.radius.element, s);
}

} // namespace skin
