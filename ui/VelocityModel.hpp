#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace shell {
// The legacy config stores the maximum INPUT velocity for each output key.
// Editor samples instead describe output (0..31) at 32 evenly spaced inputs.
struct VelocityPreset {
    std::string name;
    std::array<int, 32> thresholds{};
};
struct VelocityEdit {
    size_t preset = 1;
    float sensitivity = 0;
    float contrast = 0;
    bool manual = false;
    std::array<float, 32> samples{};
};
inline int VelocityBucket(const std::array<int, 32>& thresholds, int input) {
    int i = 0;
    while (i < 31 && thresholds[i] < input) ++i;
    return i;
}
inline float InterpolateVelocity(const std::array<float, 32>& samples, float x) {
    const float position = std::clamp(x, 0.f, 1.f) * 31;
    const int i = static_cast<int>(position), j = std::min(i + 1, 31);
    float t = position - i; t = t * t * (3 - 2 * t);
    return samples[i] + (samples[j] - samples[i]) * t;
}
inline std::array<float, 32> VelocitySamples(const VelocityPreset& preset) {
    std::array<float, 32> samples{};
    for (int i = 0; i < 32; ++i)
        samples[i] = VelocityBucket(preset.thresholds, std::max(1, static_cast<int>(std::round(i * 127.f / 31)))) / 31.f;
    return samples;
}
inline float VelocityShape(const VelocityPreset& preset, const VelocityEdit& edit, float x) {
    if (edit.manual) return InterpolateVelocity(edit.samples, x);
    const float shifted = std::clamp(x + edit.sensitivity * .0028f, 0.f, 1.f);
    const float y = InterpolateVelocity(VelocitySamples(preset), shifted);
    return std::clamp(y + (y * y * (3 - 2 * y) - y) * edit.contrast / 100, 0.f, 1.f);
}
inline bool VelocityEdited(const VelocityEdit& edit) {
    return edit.manual || edit.sensitivity != 0 || edit.contrast != 0;
}
inline std::array<int, 32> VelocityThresholds(const VelocityPreset& preset, const VelocityEdit& edit) {
    if (!VelocityEdited(edit)) return preset.thresholds; // Preserve built-ins exactly.
    std::array<float, 32> samples{};
    for (int i = 0; i < 32; ++i) samples[i] = VelocityShape(preset, edit, i / 31.f);
    std::array<int, 32> result{};
    for (int input = 1; input <= 127; ++input) {
        const int output = std::clamp(static_cast<int>(std::round(InterpolateVelocity(samples, input / 127.f) * 31)), 0, 31);
        for (int bucket = output; bucket < 32; ++bucket) result[bucket] = input;
    }
    result[31] = 127;
    return result;
}
inline std::string VelocityName(const std::vector<VelocityPreset>& presets, const VelocityEdit& edit) {
    if (edit.preset >= presets.size()) return "Linear Fine";
    return presets[edit.preset].name + (VelocityEdited(edit) ? " (edited)" : "");
}
}
