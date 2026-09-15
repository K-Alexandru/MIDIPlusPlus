#pragma once

// SkinDraw: the drawing half of Skin.hpp.
//
// ImGui's own style covers flat fills, borders and radii, so ApplyStyle() maps
// most of a Skin straight onto ImGuiStyle. What ImGui has no concept of is the
// depth system the mockup is built on: two shadows plus a top highlight on
// anything raised, an inner shadow on anything recessed. Those are drawn by
// hand into the window's draw list, which is cheap because they are rounded
// rects and lines rather than textures.

#include "../MIDI++/Skin.hpp"
#include "imgui.h"

namespace skin {

// Maps a Skin onto ImGuiStyle: colours, radii and the 4px spacing scale.
// Call once per skin change, not per frame.
void ApplyStyle(const Skin& s, float dpi = 1.f);

// Copy design metrics for draw-list geometry. Typography remains in design
// pixels and is scaled by ImGuiStyle::FontScaleDpi, never twice.
Skin ScaleGeometry(const Skin& s, float dpi);

// A raised surface: ambient shadow, contact shadow, fill, hairline border and
// a one-pixel lighter line inside the top edge. This is the card, the control
// and the popover, differing only in radius.
//
// The ambient shadow is approximated by stacking a few translucent rounded
// rects rather than blurring, which is what a draw list can do cheaply.
void RaisedRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding,
                const Skin& s, ImU32 fill, bool topHighlight = true);

// A recessed surface: fill, inner shadow along the top edge, hairline border.
// Anything you type into, read from or fill up, so fields, lists, the track
// table, the curve graph and slider grooves.
// Pass shadow = false when content will cover the top edge; RoundCorners then
// draws the shadow over that content instead.
void RecessedRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding,
                  const Skin& s, bool shadow = true);

// Convenience wrappers that take the current window's draw list and the given
// rect in screen space.
void RaisedPanel(ImVec2 min, ImVec2 max, const Skin& s);
void RecessedField(ImVec2 min, ImVec2 max, const Skin& s, bool shadow = true);

// Draw after square content inside a rounded field (list rows, table headers,
// a scrollbar): paints `outside`, the colour around the field, into the four
// corners beyond the curve, optionally the inner shadow on top of the content,
// and the hairline, so the content reads as sitting inside the rounded field.
// Use the draw list the content went to.
void RoundCorners(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, ImU32 outside, const Skin& s,
                  bool shadow = false);

} // namespace skin
