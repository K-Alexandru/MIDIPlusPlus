#pragma once
// Themes: a named pair of skins, built in or the user's own.
//
// Free of ImGui and Win32, like Skin.hpp, so a test can hold it to the design
// in SHELL-GAPS.md. A custom theme is colour only: shape, spacing and type are
// skin::Shape's and never reach themes.json.

#include "../MIDI++/Skin.hpp"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace shell {

// Every colour a skin holds, under the key themes.json saves it by and the
// plain name the editor shows. `derived` colours follow another one, an
// accent at an alpha or a shadow, and sit under one collapsed row.
struct ThemeColour {
    const char* key;
    const char* label;
    const char* group;
    bool derived;
    skin::Argb& (*at)(skin::Skin&);
};

inline const std::array<ThemeColour, 24>& ThemeColours() {
    using S = skin::Skin;
    static const std::array<ThemeColour, 24> colours{{
        {"canvas", "Window", "Background", false, [](S& s) -> skin::Argb& { return s.surface.canvas; }},
        {"structure", "Bars", "Background", false, [](S& s) -> skin::Argb& { return s.surface.structure; }},
        {"card", "Panels", "Background", false, [](S& s) -> skin::Argb& { return s.surface.card; }},
        {"elevated", "Controls", "Background", false, [](S& s) -> skin::Argb& { return s.surface.elevated; }},
        {"elevatedHot", "Hover", "Background", false, [](S& s) -> skin::Argb& { return s.surface.elevatedHot; }},
        {"recessed", "Fields", "Background", false, [](S& s) -> skin::Argb& { return s.surface.recessed; }},
        {"inkPrimary", "Text", "Text", false, [](S& s) -> skin::Argb& { return s.ink.primary; }},
        {"inkSecondary", "Secondary text", "Text", false, [](S& s) -> skin::Argb& { return s.ink.secondary; }},
        {"inkTertiary", "Faint text", "Text", false, [](S& s) -> skin::Argb& { return s.ink.tertiary; }},
        {"accent", "Accent", "Colour", false, [](S& s) -> skin::Argb& { return s.accent.accent; }},
        {"ok", "On", "Colour", false, [](S& s) -> skin::Argb& { return s.accent.ok; }},
        {"okInk", "On text", "Colour", false, [](S& s) -> skin::Argb& { return s.accent.okInk; }},
        {"warn", "Warning", "Colour", false, [](S& s) -> skin::Argb& { return s.accent.warn; }},
        {"bad", "Error", "Colour", false, [](S& s) -> skin::Argb& { return s.accent.bad; }},
        {"accentSoft", "Accent fill", "Fine detail", true, [](S& s) -> skin::Argb& { return s.accent.accentSoft; }},
        {"accentLine", "Accent outline", "Fine detail", true, [](S& s) -> skin::Argb& { return s.accent.accentLine; }},
        {"okSoft", "On fill", "Fine detail", true, [](S& s) -> skin::Argb& { return s.accent.okSoft; }},
        {"okBorder", "On outline", "Fine detail", true, [](S& s) -> skin::Argb& { return s.accent.okBorder; }},
        {"hairline", "Edges", "Fine detail", true, [](S& s) -> skin::Argb& { return s.border.hairline; }},
        {"strong", "Popup edges", "Fine detail", true, [](S& s) -> skin::Argb& { return s.border.strong; }},
        {"topHighlight", "Top highlight", "Fine detail", true, [](S& s) -> skin::Argb& { return s.border.topHighlight; }},
        {"contact", "Edge shadow", "Fine detail", true, [](S& s) -> skin::Argb& { return s.contact.colour; }},
        {"ambient", "Soft shadow", "Fine detail", true, [](S& s) -> skin::Argb& { return s.ambient.colour; }},
        {"inner", "Inset shadow", "Fine detail", true, [](S& s) -> skin::Argb& { return s.inner.colour; }},
    }};
    return colours;
}

// OKLCH, Björn Ottosson's OKLab in polar form: equal steps of L look like
// equal steps of lightness, which HSL's do not, and that is the whole reason
// a derived palette keeps its depth.
struct Lch { double l, c, h; };

namespace theme_detail {
inline double ToLinear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
inline double FromLinear(double v) { return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055; }
inline bool LinearFromLch(const Lch& lch, double& r, double& g, double& b) {
    const double a = lch.c * std::cos(lch.h), bb = lch.c * std::sin(lch.h);
    const double l_ = lch.l + 0.3963377774 * a + 0.2158037573 * bb;
    const double m_ = lch.l - 0.1055613458 * a - 0.0638541728 * bb;
    const double s_ = lch.l - 0.0894841775 * a - 1.2914855480 * bb;
    const double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    r = +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    const double slack = 1e-4;
    return r >= -slack && r <= 1 + slack && g >= -slack && g <= 1 + slack && b >= -slack && b <= 1 + slack;
}
}

inline Lch ToLch(skin::Argb colour) {
    using namespace theme_detail;
    const double r = ToLinear(((colour >> 16) & 0xFF) / 255.0), g = ToLinear(((colour >> 8) & 0xFF) / 255.0),
                 b = ToLinear((colour & 0xFF) / 255.0);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const double L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    const double a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    return {L, std::hypot(a, bb), std::atan2(bb, a)};
}

// Opaque. A colour outside sRGB loses chroma, never hue or lightness, until
// it fits: clipping the channels instead turns a dark orange brown-green.
inline skin::Argb FromLch(Lch lch) {
    using namespace theme_detail;
    lch.l = std::clamp(lch.l, 0.0, 1.0);
    double r = 0, g = 0, b = 0;
    if (!LinearFromLch(lch, r, g, b)) {
        double low = 0, high = lch.c;
        for (int i = 0; i < 24; ++i) {
            const double mid = (low + high) / 2;
            (LinearFromLch({lch.l, mid, lch.h}, r, g, b) ? low : high) = mid;
        }
        LinearFromLch({lch.l, low, lch.h}, r, g, b);
    }
    const auto channel = [](double v) {
        return static_cast<uint32_t>(std::lround(std::clamp(FromLinear(std::clamp(v, 0.0, 1.0)), 0.0, 1.0) * 255.0));
    };
    return 0xFF000000u | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

inline skin::Argb WithAlphaOf(skin::Argb colour, skin::Argb alphaFrom) {
    return (alphaFrom & 0xFF000000u) | (colour & 0x00FFFFFFu);
}

// The built-in whose lightness a derived palette takes, role by role.
inline skin::Skin ThemeTemplate(bool dark) { return dark ? skin::BlueDark() : skin::Blue(); }

// The other half of a pair. Each colour keeps its hue and chroma and takes
// its lightness from the same role in the other mode's built-in, which is
// what keeps a card above the canvas in both: flipping lightness colour by
// colour would put it below. The translucent accents follow the new accent
// at the template's alpha. Edges, highlights and shadows are black at an
// alpha in one mode and white in the other, so they are the template's own.
inline skin::Skin OppositeSkin(const skin::Skin& from) {
    skin::Skin out = ThemeTemplate(!from.dark), source = from;
    const skin::Skin pattern = out;
    out.name = {};
    skin::Skin lightness = pattern;
    for (const auto& colour : ThemeColours()) {
        if (colour.derived) continue;
        Lch lch = ToLch(colour.at(source));
        lch.l = ToLch(colour.at(lightness)).l;
        colour.at(out) = FromLch(lch);
    }
    out.accent.accentSoft = WithAlphaOf(out.accent.accent, pattern.accent.accentSoft);
    out.accent.accentLine = WithAlphaOf(out.accent.accent, pattern.accent.accentLine);
    out.accent.okSoft = WithAlphaOf(out.accent.ok, pattern.accent.okSoft);
    out.accent.okBorder = WithAlphaOf(out.accent.ok, pattern.accent.okBorder);
    return out;
}

// A whole palette from three colours. The background is the window; every
// other surface sits as far above or below it as the template's does. The
// text is the main text, and the two quieter inks are it mixed toward the
// background by the template's proportions. On, Warning and Error keep the
// template's: they mean the same thing in every theme.
inline skin::Skin GenerateSkin(skin::Argb background, skin::Argb text, skin::Argb accent) {
    const Lch ground = ToLch(background), ink = ToLch(text);
    const bool dark = ground.l < 0.5;
    skin::Skin out = ThemeTemplate(dark), pattern = out;
    out.name = {};
    const double patternGround = ToLch(pattern.surface.canvas).l, patternInk = ToLch(pattern.ink.primary).l;
    for (const auto& colour : ThemeColours()) {
        const std::string group = colour.group;
        const double role = ToLch(colour.at(pattern)).l;
        if (group == "Background")
            colour.at(out) = FromLch({ground.l + role - patternGround, ground.c, ground.h});
        else if (group == "Text") {
            const double t = (role - patternInk) / (patternGround - patternInk);
            colour.at(out) = FromLch({ink.l + (ground.l - ink.l) * t, ink.c + (ground.c - ink.c) * t, ink.h});
        }
    }
    out.accent.accent = 0xFF000000u | (accent & 0x00FFFFFFu);
    out.accent.accentSoft = WithAlphaOf(accent, pattern.accent.accentSoft);
    out.accent.accentLine = WithAlphaOf(accent, pattern.accent.accentLine);
    return out;
}

// Changes when any colour does; never zero, so zero can mean nothing applied.
inline uint64_t SkinSignature(skin::Skin skin) {
    uint64_t hash = 1469598103934665603ull ^ (skin.dark ? 1u : 0u);
    for (const auto& colour : ThemeColours()) hash = (hash ^ colour.at(skin)) * 1099511628211ull;
    return hash | 1;
}

inline std::string ColourText(skin::Argb colour) {
    char text[10];
    const unsigned alpha = colour >> 24;
    if (alpha == 0xFF) std::snprintf(text, sizeof(text), "#%06X", static_cast<unsigned>(colour & 0xFFFFFF));
    else std::snprintf(text, sizeof(text), "#%06X%02X", static_cast<unsigned>(colour & 0xFFFFFF), alpha);
    return text;
}

inline bool ParseColour(const std::string& text, skin::Argb& colour) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#') return false;
    uint32_t value = 0;
    for (size_t i = 1; i < text.size(); ++i) {
        const char c = text[i];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return false;
        value = value << 4 | static_cast<uint32_t>(digit);
    }
    colour = text.size() == 7 ? 0xFF000000u | value : (value & 0xFF) << 24 | value >> 8;
    return true;
}

struct Theme {
    std::string id, name;
    bool builtin = false;
    // Unpaired is one palette, `onlyDark` saying which; the light/dark button
    // is disabled while it is selected. Paired and automatic, the half not
    // on screen is OppositeSkin of the one being edited.
    bool paired = true, automatic = true, onlyDark = false;
    skin::Skin light, dark;
    // The half automatic derives the other from: the one on screen when
    // automatic was turned on.
    bool sourceDark = false;
    bool ShowsDark(bool wantDark) const { return paired ? wantDark : onlyDark; }
    const skin::Skin& Shown(bool wantDark) const { return ShowsDark(wantDark) ? dark : light; }
    skin::Skin& Shown(bool wantDark) { return ShowsDark(wantDark) ? dark : light; }
    void Derive() { (sourceDark ? light : dark) = OppositeSkin(sourceDark ? dark : light); }
    // After an edit to the half on screen. The source's other half follows
    // it. An edit to the derived half is the user taking it over, so
    // automatic goes off rather than the next edit to the source undoing it.
    void Edited(bool wantDark) {
        if (!paired || !automatic) return;
        if (ShowsDark(wantDark) == sourceDark) Derive();
        else automatic = false;
    }
    void SetAutomatic(bool on, bool wantDark) {
        automatic = on;
        if (on && paired) { sourceDark = ShowsDark(wantDark); Derive(); }
    }
};

class ThemeStore {
public:
    ThemeStore() {
        themes_.push_back({"blue", "Blue", true, true, false, false, skin::Blue(), skin::BlueDark(), false});
        themes_.push_back({"orange", "Orange", true, true, false, false, skin::Orange(), skin::OrangeDark(), false});
    }
    const std::vector<Theme>& All() const { return themes_; }
    Theme* Find(const std::string& id) {
        const auto found = std::find_if(themes_.begin(), themes_.end(), [&](const Theme& theme) { return theme.id == id; });
        return found == themes_.end() ? nullptr : &*found;
    }
    // Never null: a theme that is gone is Blue.
    Theme& Active(const std::string& id) { auto* found = Find(id); return found ? *found : themes_.front(); }

    // A copy of `from` as it stands, the user's to edit.
    Theme& Add(const Theme& from, std::string name) {
        Theme theme = from;
        theme.builtin = false;
        theme.name = std::move(name);
        theme.light.name = theme.dark.name = {};
        int number = 1;
        do theme.id = "custom-" + std::to_string(number++); while (Find(theme.id));
        themes_.push_back(std::move(theme));
        return themes_.back();
    }
    bool Remove(const std::string& id) {
        const auto found = std::find_if(themes_.begin(), themes_.end(), [&](const Theme& theme) { return theme.id == id && !theme.builtin; });
        if (found == themes_.end()) return false;
        themes_.erase(found);
        return true;
    }

    nlohmann::json ToJson() const {
        auto list = nlohmann::json::array();
        for (const auto& theme : themes_) {
            if (theme.builtin) continue;
            const auto palette = [](skin::Skin skin) {
                nlohmann::json colours = nlohmann::json::object();
                for (const auto& colour : ThemeColours()) colours[colour.key] = ColourText(colour.at(skin));
                return colours;
            };
            nlohmann::json entry{{"id", theme.id}, {"name", theme.name}, {"paired", theme.paired}};
            if (theme.paired) {
                entry["automatic"] = theme.automatic;
                entry["source"] = theme.sourceDark ? "dark" : "light";
                entry["light"] = palette(theme.light);
                entry["dark"] = palette(theme.dark);
            }
            else entry[theme.onlyDark ? "dark" : "light"] = palette(theme.onlyDark ? theme.dark : theme.light);
            list.push_back(std::move(entry));
        }
        return {{"themes", std::move(list)}};
    }
    // A theme that does not read is skipped and the rest are kept; a colour
    // that does not read keeps the template's.
    void FromJson(const nlohmann::json& json) {
        themes_.erase(std::remove_if(themes_.begin(), themes_.end(), [](const Theme& theme) { return !theme.builtin; }), themes_.end());
        const auto list = json.find("themes");
        if (list == json.end() || !list->is_array()) return;
        for (const auto& entry : *list) {
            if (!entry.is_object()) continue;
            Theme theme;
            theme.id = entry.value("id", std::string());
            theme.name = entry.value("name", std::string());
            if (theme.id.empty() || theme.name.empty() || Find(theme.id)) continue;
            theme.paired = entry.value("paired", true);
            theme.automatic = entry.value("automatic", false);
            theme.sourceDark = entry.value("source", std::string("light")) == "dark";
            const auto palette = [&](const char* half, bool dark) {
                skin::Skin skin = ThemeTemplate(dark);
                skin.name = {};
                const auto colours = entry.find(half);
                if (colours != entry.end() && colours->is_object())
                    for (const auto& colour : ThemeColours()) {
                        const auto text = colours->find(colour.key);
                        if (text != colours->end() && text->is_string()) ParseColour(text->get<std::string>(), colour.at(skin));
                    }
                return skin;
            };
            theme.light = palette("light", false);
            theme.dark = palette("dark", true);
            theme.onlyDark = !theme.paired && entry.contains("dark") && !entry.contains("light");
            themes_.push_back(std::move(theme));
        }
    }
    void Load(const std::filesystem::path& path) {
        try {
            std::ifstream stream(path);
            if (stream) FromJson(nlohmann::json::parse(stream));
        } catch (const std::exception&) {}
    }
    bool Save(const std::filesystem::path& path) const {
        auto temporary = path; temporary += L".tmp";
        { std::ofstream stream(temporary); stream << ToJson().dump(2) << '\n'; if (!stream) return false; }
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        return !error;
    }
private:
    std::vector<Theme> themes_;
};
}
