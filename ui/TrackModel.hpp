#pragma once
#include "midi_structures.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <string>
#include <vector>

namespace shell {
struct TrackRow {
    size_t index = 0; // Original MIDI track index, even with conductor tracks hidden.
    std::string name;
    std::string instrument;
    std::string channels;
    size_t notes = 0;
    bool piano = false;
    bool drums = false;
    bool muted = false;
    bool solo = false;
};

std::vector<TrackRow> DescribeTracks(const MidiFile& file);

inline bool TrackAudible(const TrackRow& row, bool anySolo) {
    return anySolo ? row.solo : !row.muted;
}
inline bool AnySolo(const std::vector<TrackRow>& rows) {
    return std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.solo; });
}
inline size_t SilentTracks(const std::vector<TrackRow>& rows) {
    const bool solo = AnySolo(rows);
    return std::count_if(rows.begin(), rows.end(), [solo](const auto& row) { return !TrackAudible(row, solo); });
}
inline void SoloPiano(std::vector<TrackRow>& rows) {
    for (auto& row : rows) { row.muted = !row.piano; row.solo = false; }
}
inline void UnmuteAll(std::vector<TrackRow>& rows) {
    for (auto& row : rows) { row.muted = false; row.solo = false; }
}
}
