#pragma once

// SheetExport: a score as virtual piano sheet text.
//
// "Convert to sheet" out of the YouTube to MIDI section of HANDOFF.md. It is
// the half of that pipeline that needs nothing installed: the transcription
// half wants yt-dlp, ffmpeg and a PyTorch sidecar, and none of the four are on
// this machine. This half is the mapping the app already carries, read the
// other way round.
//
// Sheet text is what people paste to each other for these games. A note is the
// character the app would type for it, notes struck together are bracketed, and
// time is spaces. That is the whole notation, and it is lossy on purpose:
// nothing carries velocity or note length.
//
// Header only and free of project references, so consuming it costs no change
// to either vcxproj.

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace sheet {

struct Note {
    double seconds = 0;   // onset, from the start of the score
    std::string name;     // "C4", "F#3", the same spelling KEY_MAPPINGS uses
};

struct Options {
    // Two onsets closer than this are one chord. Measured asynchrony in human
    // piano performance is roughly 30 to 50 ms (Goebl/Repp, see LEGIT-MODE.md),
    // so anything inside that window was played as one gesture.
    double chordWindow = 0.045;
    // Seconds per beat, 0 when unknown. With a tempo, a gap of a beat or more
    // becomes that many spaces, which is how sheet text carries rhythm at all. One
    // beat is one space, so ordinary spacing stays ordinary.
    double beatSeconds = 0;
    // Groups per line. Sheet text is read in short lines, not one long one.
    size_t groupsPerLine = 8;
    // Spaces a single gap may contribute. A long silence should not produce a
    // line of nothing but spaces.
    size_t maxGapSpaces = 4;
};

struct Result {
    std::string text;
    size_t notes = 0;      // characters written, which is notes that reached the sheet
    size_t groups = 0;     // chords and single notes written
    size_t unmapped = 0;   // notes with no key in the mapping, so dropped
    // Two notes in one chord that map to the same character are typed once:
    // "[aa]" is not a chord anyone can play. They are neither written nor
    // unmapped, so without this counter they were simply missing, and
    // notes + unmapped came up short of the score on any real file.
    size_t merged = 0;     // mapped notes that shared a chord-mate's character
};

// notes + merged + unmapped == the number of notes handed in. Nothing is lost
// silently; every note lands in exactly one of the three.

namespace detail {

// A chord's characters are sorted so the same chord always reads the same way,
// and so a sheet diffs cleanly against another take of the same piece.
// Returns how many keys the dedupe removed, which the caller must count: it
// mutates keys, so group.size() afterwards is the written count, not the
// source count.
inline size_t appendGroup(std::string& out, std::vector<std::string>& keys) {
    std::sort(keys.begin(), keys.end());
    const size_t before = keys.size();
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    const size_t merged = before - keys.size();
    if (keys.size() == 1) {
        out += keys.front();
        return merged;
    }
    out += '[';
    for (const auto& key : keys) out += key;
    out += ']';
    return merged;
}

} // namespace detail

// notes need not be sorted; a copy is taken and ordered by onset.
inline Result ToVirtualPiano(std::vector<Note> notes,
                             const std::map<std::string, std::string>& mapping,
                             const Options& options = {}) {
    Result result;
    std::stable_sort(notes.begin(), notes.end(),
                     [](const Note& a, const Note& b) { return a.seconds < b.seconds; });

    std::vector<std::string> group;
    double groupStart = 0;
    double previousEnd = 0;
    size_t onLine = 0;
    bool first = true;

    const auto flush = [&](bool last) {
        if (group.empty()) return;
        if (!first) {
            const double gap = groupStart - previousEnd;
            size_t spaces = 1;
            if (options.beatSeconds > 0 && gap > 0) {
                const auto beats = static_cast<size_t>(gap / options.beatSeconds);
                spaces = (std::max)(size_t{1}, (std::min)(beats, options.maxGapSpaces));
            }
            result.text.append(spaces, ' ');
        }
        result.merged += detail::appendGroup(result.text, group);
        result.notes += group.size();
        ++result.groups;
        previousEnd = groupStart;
        first = false;
        group.clear();
        if (++onLine >= options.groupsPerLine && !last) {
            result.text += '\n';
            onLine = 0;
            first = true; // a line break already separates the groups
        }
    };

    for (const auto& note : notes) {
        const auto found = mapping.find(note.name);
        if (found == mapping.end() || found->second.empty()) {
            ++result.unmapped;
            continue;
        }
        if (!group.empty() && note.seconds - groupStart > options.chordWindow) flush(false);
        if (group.empty()) groupStart = note.seconds;
        group.push_back(found->second);
    }
    flush(true);
    return result;
}

} // namespace sheet
