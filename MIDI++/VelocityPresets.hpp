#pragma once

// The curve editor's preset shapes, as a plain function of a name and a point
// count, so they can be tested without standing up a Win32 window.
//
// They used to live inside VelocityCurveEditor::LoadPreset, where the only way
// to find out what they produced was to open the editor and look. The bug
// below survived that way for as long as the editor has existed.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace velocity_presets {

inline constexpr int kMaxVelocity = 127;

// Every entry is a threshold: the largest input velocity that still lands in
// that output step, read by a loop that advances while table[idx] < target.
//
// Two consequences the raw formulas got wrong, and they are the same mistake
// at the two ends of the range:
//
//  - A threshold of 0 can never be selected, because inputs run 1 to 127.
//    Every preset here used to start at 0, so the quietest step was thrown
//    away under all of them, and Exponential started 0, 0, 0 and threw away
//    three: input 1 landed on step 3.
//  - A threshold that repeats the one before it names a step no input can
//    select either, for exactly the same reason.
//
// So the shape is generated and then made strictly increasing from a floor of
// 1. On a steep curve that lifts the very bottom of the shape, which is the
// trade the rule demands: a step that exists and is slightly off the ideal
// curve is worth more than a step nobody can ever play. The built-in tables in
// PlaybackCore.cpp were repaired against the same rule at the top end; see
// VELOCITY-CURVES.md.
inline std::vector<int> Build(const std::string& preset, int pointCount) {
    std::vector<int> points(static_cast<size_t>(std::max(pointCount, 1)));
    const int last = static_cast<int>(points.size()) - 1;
    for (int i = 0; i <= last; ++i) {
        const double x = last > 0 ? static_cast<double>(i) / last : 1.0;
        double y;
        if (preset == "Logarithmic")      y = std::log(x * 9 + 1) / std::log(10.0);
        else if (preset == "Exponential") y = std::pow(x, 2);
        else if (preset == "S-Curve")     y = 0.5 + 0.5 * std::tanh((x - 0.5) * 5);
        else                              y = x;   // Linear, and the fallback
        points[static_cast<size_t>(i)] = static_cast<int>(kMaxVelocity * y);
    }

    // Strictly increasing from 1, so every step is reachable. Capped so the
    // lift can never run past the top of the range.
    for (size_t i = 0; i < points.size(); ++i) {
        const int floorValue = (i == 0) ? 1 : points[i - 1] + 1;
        points[i] = std::clamp(std::max(points[i], floorValue), 1, kMaxVelocity);
    }
    // The top step takes the whole rest of the range, whatever the shape did.
    if (!points.empty()) points.back() = kMaxVelocity;
    return points;
}

} // namespace velocity_presets
