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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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

// ---------------------------------------------------------------------------
// Styled sheets
//
// Ported from ArijanJ's midi-converter, github.com/ArijanJ/midi-converter,
// mainly src/utils/VP.js and src/utils/Rendering.js. MIT licence, Copyright
// (c) 2024 ArijanJ; the notice is in third_party/midi-converter/LICENSE. It is
// translated rather than linked: the behaviour follows the original, and every
// place it deliberately does not says so.
//
// Beyond ToVirtualPiano above: a chained quantize window that writes a spread
// chord in braces, two chord orders, out-of-range notes kept and marked rather
// than dropped, rhythm colours, tempo separators and BPM comments, line breaks
// by bar, and a whole-sheet transposition search.

enum class Rhythm { SixtyFourth, ThirtySecond, Sixteenth, Eighth, Quarter, Half, Whole, Quadruple, Long };
// Where shifted characters, and out-of-range notes, sit inside a chord.
// InOrder leaves shifted characters in pitch order, and puts low out-of-range
// notes first and high ones last.
enum class Place { Start, End, InOrder };
// Bars breaks the line at each bar and at every tempo or meter change, the
// original's "realistic". Beats breaks every StyleOptions::beats beats, its
// "manual".
enum class Breaks { Bars, Beats, None };
enum class BpmStyle { Detailed, Simple };

struct TimedNote { double seconds = 0; int midi = 0; };
struct TempoMark { double seconds = 0; double bpm = 120; };
struct MeterMark { double seconds = 0; int numerator = 4; };

struct StyleOptions {
    double quantizeMs = 35;          // a note this close to the previous one joins its chord
    bool sequentialQuantize = true;  // a spread chord keeps the order it was played in
    bool curlyQuantizes = true;      // and is written in braces rather than brackets
    bool classicChordOrder = false;  // the ordering of the first VP converters
    Place shifts = Place::Start;
    Place outOfRangePlace = Place::InOrder;
    bool showOutOfRange = true;
    bool outOfRangeMarks = false;
    std::string outOfRangeSeparator = ":";
    bool tempoMarks = false;         // rhythm separators instead of single spaces
    bool bpmChanges = true;
    BpmStyle bpmStyle = BpmStyle::Detailed;
    int minSpeedChange = 10;         // percent below which a tempo change is not mentioned
    Breaks breaks = Breaks::Bars;
    int beats = 4;
    double missingBpm = 120;         // before the first tempo mark, or when there is none
    int transpose = 0;               // semitones, applied before mapping
    bool autoTranspose = false;      // search around transpose for the best fit
    int resilience = 2;              // how much better a transposition must score to win
};

struct Segment { std::string text; bool outOfRange = false; };

struct StyledItem {
    enum class Kind { Chord, Break, Comment };
    Kind kind = Kind::Chord;
    std::vector<Segment> segments;   // a chord as written, brackets and marks included
    std::string text;                // a comment
    std::string separator;           // what follows a chord
    Rhythm rhythm = Rhythm::Long;
    double ms = 0;                   // onset of the chord's first note
    double beatMs = 500;             // length of a beat at that onset
};

struct StyledResult {
    std::vector<StyledItem> items;
    std::string text;
    size_t notes = 0;      // characters written for real notes
    size_t groups = 0;     // chords and single notes written
    size_t merged = 0;     // a pitch repeated inside one struck chord, written once
    size_t unmapped = 0;   // outside A0 to C8, or missing from the mapping; written as _
    size_t hidden = 0;     // out-of-range notes left out because showOutOfRange is off
    int transposition = 0;
    bool hasTempo = false;
};
// notes + merged + unmapped + hidden == the number of notes handed in.

// The shell's tempo and meter events are timed in ticks. These turn them into
// seconds for a ticks-per-quarter division. SMPTE files are not handled.
struct TickTempo { uint64_t tick = 0; uint32_t microsecondsPerQuarter = 500000; };
struct TickMeter { uint64_t tick = 0; int numerator = 4; };

// tempos must be sorted by tick.
inline double SecondsAtTick(uint64_t tick, const std::vector<TickTempo>& tempos, uint16_t division) {
    if (division == 0) return 0;
    double seconds = 0, microseconds = 500000;
    uint64_t from = 0;
    for (const auto& tempo : tempos) {
        if (tempo.tick >= tick) break;
        seconds += static_cast<double>(tempo.tick - from) * microseconds / division / 1e6;
        from = tempo.tick;
        microseconds = tempo.microsecondsPerQuarter;
    }
    return seconds + static_cast<double>(tick - from) * microseconds / division / 1e6;
}

inline std::vector<TempoMark> TempoMarksFromTicks(std::vector<TickTempo> tempos, uint16_t division) {
    std::stable_sort(tempos.begin(), tempos.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    std::vector<TempoMark> marks;
    for (const auto& tempo : tempos) {
        if (tempo.microsecondsPerQuarter == 0) continue;
        marks.push_back({SecondsAtTick(tempo.tick, tempos, division), 60e6 / tempo.microsecondsPerQuarter});
    }
    return marks;
}

inline std::vector<MeterMark> MeterMarksFromTicks(const std::vector<TickMeter>& meters, std::vector<TickTempo> tempos, uint16_t division) {
    std::stable_sort(tempos.begin(), tempos.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    std::vector<MeterMark> marks;
    for (const auto& meter : meters) marks.push_back({SecondsAtTick(meter.tick, tempos, division), meter.numerator});
    return marks;
}

namespace detail {

inline constexpr std::string_view kCapitals = "!@#$%^&*()QWERTYUIOPASDFGHJKLZXCBVNM";
inline constexpr std::string_view kLowercase = "1234567890qwertyuiopasdfghjklzxcvbnm";
// The original's characters for the notes a 61-key layout lacks, used when the
// mapping carries none for them.
inline constexpr std::string_view kLowOutOfRange = "1234567890qwert";   // A0 to B1
inline constexpr std::string_view kHighOutOfRange = "yuiopasdfghj";     // C#7 to C8

inline std::string NoteName(int midi) {
    static constexpr const char* names[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(names[midi % 12]) + std::to_string(midi / 12 - 1);
}
inline bool OneOf(const std::string& character, std::string_view set) {
    return character.size() == 1 && set.find(character[0]) != std::string_view::npos;
}

struct Placed {
    double ms = 0;
    double beatMs = 500;
    int midi = 0;
    std::string character;
    bool valid = false;
    bool outOfRange = false;
    int display = 0;   // the sort key the original calls displayValue
};

inline Placed Locate(double ms, int midi, const std::map<std::string, std::string>& mapping, const StyleOptions& o) {
    Placed p;
    p.ms = ms;
    p.midi = midi;
    p.valid = midi >= 21 && midi <= 108;
    p.outOfRange = midi <= 35 || midi >= 97;
    if (p.valid) {
        const auto found = mapping.find(NoteName(midi));
        // An 88-key binding for a note outside C2 to C7 is ctrl plus a key, and
        // a sheet writes the key.
        if (found != mapping.end() && !found->second.empty())
            p.character = found->second.starts_with("ctrl+") ? found->second.substr(5) : found->second;
        else if (midi <= 35) p.character = std::string(1, kLowOutOfRange[midi - 21]);
        else if (midi >= 97) p.character = std::string(1, kHighOutOfRange[midi - 97]);
        else p.valid = false;
    }
    p.display = midi;
    if (OneOf(p.character, kCapitals)) {
        if (o.shifts == Place::Start) p.display = midi - 108;
        else if (o.shifts == Place::End) p.display = midi + 108;
    } else if (p.outOfRange) {
        if (o.outOfRangePlace == Place::Start) p.display = midi - 1024;
        else if (o.outOfRangePlace == Place::End) p.display = midi + 1024;
        else p.display = OneOf(p.character, kLowOutOfRange) ? midi - 1024 : midi + 1024;
    }
    return p;
}

inline std::vector<Placed> ClassicOrder(std::vector<Placed> notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.midi < b.midi; });
    std::vector<Placed> start, end, numeric, upper, lower;
    const Placed* last = nullptr;
    for (const auto& n : notes) {
        if (n.outOfRange) {
            if (n.display == n.midi - 1024) start.push_back(n);
            else if (n.display == n.midi + 1024) end.push_back(n);
            continue;
        }
        if (n.character.size() == 1 && n.character[0] >= '0' && n.character[0] <= '9') numeric.push_back(n);
        else if (OneOf(n.character, kCapitals)) upper.push_back(n);
        else lower.push_back(n);
        last = &n;
    }
    std::vector<Placed> out(start);
    const auto append = [&](const std::vector<Placed>& part) { out.insert(out.end(), part.begin(), part.end()); };
    if (last) {
        if (OneOf(last->character, kCapitals)) { append(numeric); append(lower); append(upper); }
        else { append(upper); append(numeric); append(lower); }
    }
    append(end);
    return out;
}

inline std::vector<Placed> SortChord(std::vector<Placed> notes, bool classic) {
    if (classic) return ClassicOrder(std::move(notes));
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.display < b.display; });
    return notes;
}

inline StyledItem RenderChord(const std::vector<Placed>& chord, bool quantized, const StyleOptions& o, StyledResult& r) {
    StyledItem item;
    const auto nonOutOfRange = static_cast<size_t>(std::count_if(chord.begin(), chord.end(), [](const auto& n) { return !n.outOfRange; }));
    bool isChord = chord.size() > 1 && std::any_of(chord.begin(), chord.end(), [](const auto& n) { return n.valid; });
    if (!o.showOutOfRange && nonOutOfRange <= 1) isChord = false;
    const bool curly = quantized && o.curlyQuantizes;
    const Placed* firstStart = nullptr;
    const Placed* lastStart = nullptr;
    const Placed* firstEnd = nullptr;
    for (const auto& n : chord) {
        if (!n.outOfRange) continue;
        if (n.display == n.midi - 1024) { if (!firstStart) firstStart = &n; lastStart = &n; }
        else if (n.display == n.midi + 1024 && !firstEnd) firstEnd = &n;
    }
    if (isChord) item.segments.push_back({curly ? "{" : "["});
    for (const auto& n : chord) {
        if (!n.valid) { item.segments.push_back({"_"}); ++r.unmapped; continue; }
        const bool drawOor = n.outOfRange && o.showOutOfRange;
        if (drawOor) {
            const bool marked = &n == firstStart ||
                (!isChord && &n == firstEnd) ||
                (isChord && nonOutOfRange == 0 && !firstStart && &n == firstEnd) ||
                (isChord && nonOutOfRange > 0 && &n == firstEnd);
            std::string text = n.character;
            if (o.outOfRangeMarks && marked) text = o.outOfRangeSeparator + text;
            item.segments.push_back({text, true});
            ++r.notes;
            if (o.outOfRangeMarks && &n == lastStart && nonOutOfRange > 0) item.segments.push_back({"'"});
        } else if (!n.outOfRange) {
            item.segments.push_back({n.character});
            ++r.notes;
        } else {
            ++r.hidden;
        }
    }
    if (isChord) item.segments.push_back({curly ? "}" : "]"});
    ++r.groups;
    return item;
}

inline int TranspositionScore(const std::vector<TimedNote>& notes, int by, const std::map<std::string, std::string>& mapping) {
    int good = 0, lower = 0, upper = 0;
    for (const auto& note : notes) {
        const auto p = Locate(0, note.midi + by, mapping, {});
        if (p.outOfRange || !p.valid) continue;
        ++good;
        if (OneOf(p.character, kLowercase)) ++lower; else ++upper;
    }
    return good * 2 + std::abs(upper - lower);
}

// The original's best_transposition_for_monochord over the whole sheet, with
// its search of eleven semitones either way.
inline int BestTransposition(const std::vector<TimedNote>& notes, const std::map<std::string, std::string>& mapping, int stickTo, int resilience) {
    int best = TranspositionScore(notes, stickTo, mapping);
    std::vector<int> bests{stickTo};
    const auto consider = [&](int n) {
        const int score = TranspositionScore(notes, n, mapping);
        if (score > best + resilience) { best = score; bests = {n}; }
        else if (score == best) {
            if (n == 0 && std::find(bests.begin(), bests.end(), 0) != bests.end()) return;
            bests.push_back(n);
        }
    };
    for (int i = stickTo; i <= stickTo + 11; ++i) { consider(i); consider(-i); }
    return bests.front();
}

inline std::optional<std::string> BpmComment(long previous, long next, BpmStyle style, int minimum) {
    const bool faster = next > previous;
    const double larger = static_cast<double>(faster ? next : previous);
    const double smaller = static_cast<double>(faster ? previous : next);
    if (smaller <= 0) return std::nullopt;
    const long percent = std::lround((larger - smaller) / smaller * 100);
    if (percent < minimum) return std::nullopt;
    const std::string word = faster ? "faster" : "slower";
    if (style == BpmStyle::Simple) {
        const char mark = faster ? '>' : '<';
        const long increments = (std::max)(1L, percent / 10);
        if (increments > 20) return std::string(1, mark) + " " + std::to_string(percent) + "% " + word + " " + mark;
        return std::string(static_cast<size_t>(increments), mark);
    }
    return std::to_string(percent) + "% " + word + " - BPM changed to " + std::to_string(next);
}

inline Rhythm RhythmFor(double beat, double difference) {
    if (difference < beat / 16) return Rhythm::SixtyFourth;
    if (difference < beat / 8) return Rhythm::ThirtySecond;
    if (difference < beat / 4) return Rhythm::Sixteenth;
    if (difference < beat / 2) return Rhythm::Eighth;
    if (difference < beat) return Rhythm::Quarter;
    if (difference < beat * 2) return Rhythm::Half;
    if (difference < beat * 4) return Rhythm::Whole;
    if (difference < beat * 8) return Rhythm::Quadruple;
    return Rhythm::Long;
}

inline std::string Separator(double beat, double difference) {
    if (difference < beat / 4) return "-";
    if (difference < beat / 2) return " ";
    if (difference < beat) return " - ";
    if (difference < beat * 2) return ", ";
    if (difference < beat * 3) return "... ";
    if (difference < beat * 4) return ".... ";
    return "...... ";
}

// Trailing spaces go, and runs of blank lines become one. The original copies
// the page's innerText as it stands; a pasted sheet reads better without them.
inline std::string TidyLines(const std::string& text) {
    std::vector<std::string> lines(1);
    for (char c : text) {
        if (c == '\n') lines.emplace_back();
        else lines.back() += c;
    }
    std::string out;
    bool started = false, blank = false;
    for (auto& line : lines) {
        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (line.empty()) { blank = started; continue; }
        if (started) out += blank ? "\n\n" : "\n";
        out += line;
        started = true;
        blank = false;
    }
    return out;
}

inline std::string EscapeHtml(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

} // namespace detail

inline StyledResult Style(std::vector<TimedNote> notes, const std::map<std::string, std::string>& mapping,
                          std::vector<TempoMark> tempos = {}, std::vector<MeterMark> meters = {},
                          const StyleOptions& o = {}) {
    using Kind = StyledItem::Kind;
    StyledResult r;
    const auto bySeconds = [](const auto& a, const auto& b) { return a.seconds < b.seconds; };
    std::stable_sort(notes.begin(), notes.end(), bySeconds);
    std::stable_sort(tempos.begin(), tempos.end(), bySeconds);
    std::stable_sort(meters.begin(), meters.end(), bySeconds);
    r.hasTempo = !tempos.empty();
    r.transposition = o.autoTranspose && !notes.empty()
        ? detail::BestTransposition(notes, mapping, o.transpose, o.resilience) : o.transpose;

    const auto comment = [&](std::string text) {
        StyledItem item; item.kind = Kind::Comment; item.text = std::move(text);
        r.items.push_back(std::move(item));
    };
    const auto lineBreak = [&] { StyledItem item; item.kind = Kind::Break; r.items.push_back(std::move(item)); };
    if (r.transposition != 0) comment("Transpose by: " + std::to_string(-r.transposition));

    // Beats elapsed at a time. The original counts ticks; this is the same
    // count read from seconds, because the notes arrive timed.
    const auto beatsAt = [&](double seconds) {
        double beats = 0, from = 0, bpm = o.missingBpm;
        for (const auto& mark : tempos) {
            if (mark.seconds >= seconds) break;
            beats += (mark.seconds - from) * bpm / 60;
            from = mark.seconds;
            bpm = mark.bpm;
        }
        return beats + (seconds - from) * bpm / 60;
    };

    struct Event { double seconds; int order; size_t index; };   // order: tempo, meter, note
    std::vector<Event> events;
    for (size_t i = 0; i < tempos.size(); ++i) events.push_back({tempos[i].seconds, 0, i});
    for (size_t i = 0; i < meters.size(); ++i) events.push_back({meters[i].seconds, 1, i});
    for (size_t i = 0; i < notes.size(); ++i) events.push_back({notes[i].seconds, 2, i});
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.seconds != b.seconds ? a.seconds < b.seconds : a.order < b.order;
    });

    // The original's nextTempo starts at bpm * 4166.66, which is 500000 only
    // at 120 BPM and grows the wrong way from there. A beat here is 60000 / BPM.
    double bpm = o.missingBpm, previousBpm = 0;
    bool haveTempo = false, scheduled = false;
    int numerator = 4;
    enum class Next { Start, Unset, Set } next = Next::Start;
    double nextBar = 0, penalty = 0;
    std::vector<detail::Placed> current;
    bool haveLast = false;
    double last = 0;

    const auto flush = [&] {
        if (current.empty()) return;
        bool quantized = false;
        for (size_t i = 1; i < current.size(); ++i)
            if (current[i].ms != current[i - 1].ms) { quantized = true; break; }
        std::vector<detail::Placed> chord;
        if (!quantized) {
            for (const auto& n : current) {
                if (std::any_of(chord.begin(), chord.end(), [&](const auto& kept) { return kept.midi == n.midi; })) { ++r.merged; continue; }
                chord.push_back(n);
            }
            chord = detail::SortChord(std::move(chord), o.classicChordOrder);
        } else if (o.sequentialQuantize) {
            chord = current;
            std::stable_sort(chord.begin(), chord.end(), [](const auto& a, const auto& b) { return a.ms < b.ms; });
        } else {
            chord = detail::SortChord(current, o.classicChordOrder);
        }
        auto item = detail::RenderChord(chord, quantized, o, r);
        item.ms = chord.front().ms;
        item.beatMs = chord.front().beatMs;
        r.items.push_back(std::move(item));
        current.clear();
    };
    const auto checkBreak = [&](const detail::Placed& first) {
        if (o.breaks == Breaks::Bars) {
            const double beat = beatsAt(first.ms / 1000);
            if (scheduled) {
                scheduled = false;
                lineBreak();
                if (next != Next::Start) next = Next::Unset;
            }
            if (next != Next::Set) { nextBar = (next == Next::Unset ? beat : 0) + numerator; next = Next::Set; }
            if (beat + 1e-9 >= nextBar) { lineBreak(); nextBar += numerator; }
        } else if (o.breaks == Breaks::Beats) {
            const double normalized = first.ms - penalty;
            if (normalized + 0.5 >= first.beatMs * o.beats) { lineBreak(); penalty += normalized; }
        }
    };

    for (const auto& event : events) {
        if (event.order == 0) {
            const double newBpm = tempos[event.index].bpm;
            // The original breaks the line at a tempo change as well as at a
            // meter change.
            scheduled = true;
            if (o.bpmChanges) {
                if (!haveTempo) comment("Tempo: " + std::to_string(std::lround(newBpm)) + " BPM");
                else if (newBpm != previousBpm)
                    if (auto text = detail::BpmComment(std::lround(previousBpm), std::lround(newBpm), o.bpmStyle, o.minSpeedChange))
                        comment(*text);
            }
            haveTempo = true;
            previousBpm = bpm = newBpm;
            continue;
        }
        if (event.order == 1) {
            numerator = (std::max)(1, meters[event.index].numerator);
            scheduled = true;
            continue;
        }
        const auto& note = notes[event.index];
        auto placed = detail::Locate(note.seconds * 1000, note.midi + r.transposition, mapping, o);
        placed.beatMs = bpm > 0 ? 60000 / bpm : 500;
        if (!haveLast) { haveLast = true; last = placed.ms; }
        // Chained: each note is measured against the one before it, not
        // against the chord's first note.
        if (std::abs(placed.ms - last) < o.quantizeMs) { current.push_back(placed); last = placed.ms; }
        else { flush(); current.push_back(placed); last = placed.ms; }
        checkBreak(current.front());
    }
    flush();

    // Leading, trailing and doubled breaks go, as the original's combined
    // export does.
    std::vector<StyledItem> kept;
    for (auto& item : r.items) {
        if (item.kind == Kind::Break && (kept.empty() || kept.back().kind == Kind::Break)) continue;
        kept.push_back(std::move(item));
    }
    while (!kept.empty() && kept.back().kind == Kind::Break) kept.pop_back();
    r.items = std::move(kept);

    std::vector<size_t> chords;
    for (size_t i = 0; i < r.items.size(); ++i)
        if (r.items[i].kind == Kind::Chord) chords.push_back(i);
    for (size_t c = 0; c < chords.size(); ++c) {
        auto& item = r.items[chords[c]];
        // The last chord has nothing after it, so no separator. The original
        // writes its longest one there.
        if (c + 1 == chords.size()) { item.rhythm = Rhythm::Long; item.separator.clear(); continue; }
        const double difference = r.items[chords[c + 1]].ms - item.ms - 0.5;
        // The original takes the half millisecond off twice on the way to the
        // colour, and once for the separator.
        item.rhythm = detail::RhythmFor(item.beatMs, difference - 0.5);
        item.separator = o.tempoMarks ? detail::Separator(item.beatMs, difference) : " ";
    }

    std::string text;
    for (size_t i = 0; i < r.items.size(); ++i) {
        const auto& item = r.items[i];
        if (item.kind == Kind::Chord) {
            for (const auto& segment : item.segments) text += segment.text;
            text += item.separator;
        } else if (item.kind == Kind::Comment) {
            text += '\n' + item.text + '\n';
        } else {
            const bool nearComment = (i > 0 && r.items[i - 1].kind == Kind::Comment) ||
                (i + 1 < r.items.size() && r.items[i + 1].kind == Kind::Comment);
            if (!nearComment) text += '\n';
        }
    }
    r.text = detail::TidyLines(text);
    return r;
}

// The original's colours, long notes green through short notes red.
inline const char* RhythmColour(Rhythm rhythm) {
    switch (rhythm) {
    case Rhythm::Quadruple: return "#a3f0a3";
    case Rhythm::Whole: return "#74da74";
    case Rhythm::Half: return "#9ada5a";
    case Rhythm::Quarter: return "#c0c05a";
    case Rhythm::Eighth: return "#da7e5a";
    case Rhythm::Sixteenth: return "#daa6a6";
    case Rhythm::ThirtySecond: return "#ff1900";
    case Rhythm::SixtyFourth: return "#9c0f00";
    default: return "white";
    }
}

// A coloured sheet as one self-contained HTML page, on the original's
// background. Out-of-range notes are underlined and heavy, as it draws them.
inline std::string ToHtml(const StyledResult& r) {
    using Kind = StyledItem::Kind;
    std::string html = "<!doctype html><meta charset=\"utf-8\"><title>Sheet</title>"
        "<body style=\"margin:16px;background:#2D2A32;color:#ffffff;font-family:Verdana,sans-serif;"
        "font-size:10pt;line-height:135%\"><div style=\"white-space:pre-wrap\">";
    for (size_t i = 0; i < r.items.size(); ++i) {
        const auto& item = r.items[i];
        if (item.kind == Kind::Chord) {
            html += std::string("<span style=\"color:") + RhythmColour(item.rhythm) + "\">";
            for (const auto& segment : item.segments) {
                if (segment.outOfRange)
                    html += "<span style=\"display:inline-flex;justify-content:center;min-width:0.6em;"
                            "border-bottom:2px solid;font-weight:900\">" + detail::EscapeHtml(segment.text) + "</span>";
                else html += detail::EscapeHtml(segment.text);
            }
            html += detail::EscapeHtml(item.separator) + "</span>";
        } else if (item.kind == Kind::Comment) {
            html += "<br><span style=\"color:#c8c4cc\">" + detail::EscapeHtml(item.text) + "</span><br>";
        } else {
            const bool nearComment = (i > 0 && r.items[i - 1].kind == Kind::Comment) ||
                (i + 1 < r.items.size() && r.items[i + 1].kind == Kind::Comment);
            if (!nearComment) html += "<br>";
        }
    }
    return html + "</div></body>";
}

} // namespace sheet
