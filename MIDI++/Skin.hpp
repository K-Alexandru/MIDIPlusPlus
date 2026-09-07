#pragma once

// Skin: the four visual parameter sets from skin-system.html as runtime data.
//
// Deliberately free of ImGui, GDI+ and Win32 so it can be included from the
// current UI, from the ImGui shell when that exists, and from a test. Nothing
// here is constexpr: skins are switched at runtime and a future custom skin is
// just another instance.
//
// Every value is lifted from the mockup, which is the layout spec. See
// HANDOFF.md section 15 for why each rule exists. The short version:
//
//   * Five surface tiers, not one. Canvas, structure, card, elevated, and a
//     recessed tier for anything you type into, read from or fill up.
//   * Raised surfaces carry two shadows and a one-pixel top highlight;
//     recessed surfaces carry an inner shadow. Borders are translucent
//     hairlines, never solid greys.
//   * A strict 4px spacing scale, and concentric radii: an element inset by
//     4px inside a 12px container is drawn at 8px.
//   * Accent is selection, focus, slider fills and the live curve. It is not
//     decoration and it is never the only signal for a state.

#include <array>
#include <cstdint>
#include <string_view>

namespace skin {

// 0xAARRGGBB, matching how both GDI+ and ImGui's ImColor take a packed value.
using Argb = uint32_t;

constexpr Argb Rgb(uint32_t rgb) { return 0xFF000000u | rgb; }
constexpr Argb Rgba(uint32_t rgb, double alpha) {
    return (static_cast<uint32_t>(alpha * 255.0 + 0.5) << 24) | rgb;
}

// One shadow layer. The mockup composes two of these per raised surface: a
// tight contact shadow and a soft ambient one. In a draw list the ambient
// becomes two or three stacked translucent rounded rects.
struct Shadow {
    float offsetY;
    float blur;
    Argb  colour;
};

struct Surfaces {
    Argb canvas;      // window ground
    Argb structure;   // strip, status bar
    Argb card;
    Argb elevated;    // controls
    Argb elevatedHot; // hover
    Argb recessed;    // fields, lists, tracks, slider grooves
};

struct Ink {
    Argb primary;
    Argb secondary;
    Argb tertiary;
};

struct Accents {
    Argb accent, accentSoft, accentLine;
    Argb ok, okInk, okSoft, okBorder;
    Argb warn, bad;
};

struct Borders {
    Argb hairline;    // ordinary edges
    Argb strong;      // popovers and anything floating
    Argb topHighlight;
};

struct Radii {
    float window, card, control, element;
};

// The 4px scale. Nothing in the UI may use a value that is not on it.
struct Spacing {
    float s1 = 4.f, s2 = 8.f, s3 = 12.f, s4 = 16.f, s6 = 24.f;
    float windowPad, panelPad;
};

struct Metrics {
    float controlHeight;  // 28 Classic, 32 Modern
    float innerHeight;    // 20 / 24, for things nested inside a control
};

struct TypeScale {
    float meta, body, heading, title;
    std::string_view family;
};

struct Skin {
    std::string_view name;
    bool     dark;
    Surfaces surface;
    Ink      ink;
    Accents  accent;
    Borders  border;
    Radii    radius;
    Spacing  spacing;
    Metrics  metric;
    TypeScale type;
    Shadow   contact;   // tight, sells the edge
    Shadow   ambient;   // soft, sells the elevation
    Shadow   inner;     // recessed surfaces
};

// Every skin has the same shape. A skin chooses colour and nothing else.
//
// It used not to: Classic was 28px controls, 6/8px radii and 13px Segoe UI,
// Modern was 32px controls, 12px radii and 14px IBM Plex. That is a defensible
// thing for a finished app to offer and a bad thing to carry while one is being
// built. Every panel grew a `modern ? a : b` beside each measurement, the
// window opened at two different heights, and a layout bug had to be found
// twice and fixed twice. Two of the three copies of the window height were
// fixed on 2026-09-06 and the third was missed, which is exactly the failure
// this removes.
//
// The surviving shape is what was Modern's: larger type, taller controls, more
// air. Putting per-skin metrics back later is this function moved into the
// skins that want to differ. That is a small change, and it should wait until
// there is a finished layout to vary.
inline void Shape(Skin& s) {
    s.radius  = { 12.f, 12.f, 10.f, 8.f };
    s.spacing = { 4.f, 8.f, 12.f, 16.f, 24.f, 16.f, 16.f };
    s.metric  = { 32.f, 24.f };
    s.type    = { 12.f, 14.f, 14.f, 20.f, "IBM Plex Sans" };
}

// Named for the accent, because that is now the whole of the difference.
// #0B6EC4 is a plain blue; #B5443A is a brick red rather than an orange, and
// calling it Orange would describe a colour the app does not draw.
inline Skin Blue() {
    Skin s{};
    s.name = "Blue";
    s.dark = false;
    Shape(s);
    s.surface = { Rgb(0xE8EBEF), Rgb(0xF3F5F7), Rgb(0xFFFFFF),
                  Rgb(0xFFFFFF), Rgb(0xF7F9FB), Rgb(0xE9ECF1) };
    s.ink = { Rgb(0x1B1E24), Rgb(0x5B636F), Rgb(0x8B929E) };
    s.accent = { Rgb(0x0B6EC4), Rgba(0x0B6EC4, .10), Rgba(0x0B6EC4, .35),
                 Rgb(0x1A7F45), Rgb(0x12652F), Rgba(0x1A7F45, .12), Rgba(0x1A7F45, .34),
                 Rgb(0x9A6C0D), Rgb(0xB23F2F) };
    s.border = { Rgba(0x11161F, .09), Rgba(0x11161F, .16), Rgba(0xFFFFFF, .85) };
    // Shadow geometry is shape and is shared; a shadow's colour belongs to the
    // palette it falls on, so only that is set here.
    s.contact = { 1.f, 1.f, Rgba(0x11161F, .05) };
    s.ambient = { 2.f, 6.f, Rgba(0x11161F, .04) };
    s.inner   = { 1.f, 2.f, Rgba(0x11161F, .09) };
    return s;
}

inline Skin BlueDark() {
    Skin s = Blue();
    s.name = "Blue Dark";
    s.dark = true;
    s.surface = { Rgb(0x191C21), Rgb(0x20242A), Rgb(0x272B31),
                  Rgb(0x2E333A), Rgb(0x363C44), Rgb(0x14171B) };
    s.ink = { Rgb(0xE7E9EC), Rgb(0xA3AAB5), Rgb(0x79818D) };
    s.accent = { Rgb(0x4EA3EA), Rgba(0x4EA3EA, .14), Rgba(0x4EA3EA, .40),
                 Rgb(0x4FB277), Rgb(0x8FDCAA), Rgba(0x4FB277, .16), Rgba(0x4FB277, .40),
                 Rgb(0xD0A259), Rgb(0xE0685A) };
    s.border = { Rgba(0xFFFFFF, .08), Rgba(0xFFFFFF, .14), Rgba(0xFFFFFF, .06) };
    s.contact = { 1.f, 1.f, Rgba(0x000000, .30) };
    s.ambient = { 2.f, 6.f, Rgba(0x000000, .22) };
    s.inner   = { 1.f, 2.f, Rgba(0x000000, .45) };
    return s;
}

inline Skin Terracotta() {
    Skin s{};
    s.name = "Terracotta";
    s.dark = false;
    Shape(s);
    s.surface = { Rgb(0xE6E2DB), Rgb(0xF1EEE9), Rgb(0xFBFAF8),
                  Rgb(0xFFFFFF), Rgb(0xF6F3EE), Rgb(0xECE8E1) };
    s.ink = { Rgb(0x1D1B19), Rgb(0x6F675E), Rgb(0x948B81) };
    s.accent = { Rgb(0xB5443A), Rgba(0xB5443A, .10), Rgba(0xB5443A, .32),
                 Rgb(0x3F7A55), Rgb(0x245939), Rgba(0x3F7A55, .12), Rgba(0x3F7A55, .30),
                 Rgb(0xA8781F), Rgb(0xB5443A) };
    s.border = { Rgba(0x2D261E, .09), Rgba(0x2D261E, .16), Rgba(0xFFFFFF, .90) };
    s.contact = { 1.f, 1.f, Rgba(0x2D261E, .05) };
    s.ambient = { 2.f, 6.f, Rgba(0x2D261E, .04) };
    s.inner   = { 1.f, 2.f, Rgba(0x2D261E, .08) };
    return s;
}

inline Skin TerracottaDark() {
    Skin s = Terracotta();
    s.name = "Terracotta Dark";
    s.dark = true;
    s.surface = { Rgb(0x17140F), Rgb(0x1F1B15), Rgb(0x26221B),
                  Rgb(0x2E2921), Rgb(0x372F26), Rgb(0x120F0B) };
    s.ink = { Rgb(0xEDE9E4), Rgb(0x9D938A), Rgb(0x7B736B) };
    s.accent = { Rgb(0xE0705F), Rgba(0xE0705F, .14), Rgba(0xE0705F, .36),
                 Rgb(0x6FBF8D), Rgb(0x8FD0A8), Rgba(0x6FBF8D, .14), Rgba(0x6FBF8D, .34),
                 Rgb(0xC99A4A), Rgb(0xD4574B) };
    s.border = { Rgba(0xFFFFFF, .07), Rgba(0xFFFFFF, .13), Rgba(0xFFFFFF, .05) };
    s.contact = { 1.f, 1.f, Rgba(0x000000, .35) };
    s.ambient = { 2.f, 6.f, Rgba(0x000000, .25) };
    s.inner   = { 1.f, 2.f, Rgba(0x000000, .50) };
    return s;
}

// Order is load-bearing: preferences store the index, `skin ^= 1` toggles
// light and dark, and `skin >= 2` picks the colour. Light and dark of one
// colour stay adjacent.
inline std::array<Skin, 4> All() {
    return { Blue(), BlueDark(), Terracotta(), TerracottaDark() };
}

// The piano is a physical object, not UI chrome, so ivory stays ivory in every
// skin. Only the selection highlight follows the theme.
namespace piano {
    inline constexpr Argb WhiteKeyTop    = 0xFFFFFEFB;
    inline constexpr Argb WhiteKeyBottom = 0xFFF2EEE5;
    inline constexpr Argb WhiteKeyEdge   = 0xFFB6B0A4;
    inline constexpr Argb BlackKeyTop    = 0xFF2C2926;
    inline constexpr Argb BlackKeyBottom = 0xFF131110;
    inline constexpr Argb Case           = 0xFF1F1C1A;
}

} // namespace skin
