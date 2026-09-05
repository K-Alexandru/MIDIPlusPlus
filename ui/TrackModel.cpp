#include "TrackModel.hpp"
#include "GeneralMidi.hpp"

namespace shell {
std::vector<TrackRow> DescribeTracks(const MidiFile& file) {
    struct Part {
        TrackRow row;
        std::bitset<128> programs;
        std::bitset<16> channels;
        bool nonPiano = false;
    };
    struct EventRef { const MidiEvent* event; size_t track; };
    std::vector<Part> parts(file.tracks.size());
    std::vector<EventRef> timeline;
    for (size_t i = 0; i < file.tracks.size(); ++i) {
        parts[i].row.index = i;
        parts[i].row.name = file.tracks[i].name;
        for (const auto& event : file.tracks[i].events) {
            if (event.status == 0xFF && event.data1 == 0x03)
                parts[i].row.name.assign(event.metaData.begin(), event.metaData.end());
            if ((event.status & 0xF0) == 0xC0 ||
                ((event.status & 0xF0) == 0x90 && event.data2 > 0))
                timeline.push_back({&event, i});
        }
    }
    // Program changes are channel-wide, and can live in a different MIDI track.
    // Preserve file order for events sharing a tick.
    std::stable_sort(timeline.begin(), timeline.end(), [](const auto& a, const auto& b) {
        return a.event->absoluteTick < b.event->absoluteTick;
    });
    std::array<unsigned, 16> programs{}; // General MIDI defaults to program 0.
    for (const auto& ref : timeline) {
        const auto& event = *ref.event;
        const unsigned channel = event.status & 0x0F;
        if ((event.status & 0xF0) == 0xC0) {
            programs[channel] = event.data1 & 0x7F;
            continue;
        }
        auto& part = parts[ref.track];
        ++part.row.notes;
        part.channels.set(channel);
        if (channel == 9) part.row.drums = true;
        else part.programs.set(programs[channel]);
        part.nonPiano |= channel == 9 || programs[channel] > 7;
    }
    std::vector<TrackRow> rows;
    for (auto& part : parts) {
        auto& row = part.row;
        if (!row.notes) continue;
        if (row.name.empty()) row.name = "Track " + std::to_string(row.index + 1);
        for (char& c : row.name) if (static_cast<unsigned char>(c) < 32) c = ' ';
        row.piano = !part.nonPiano;
        if (row.drums && part.programs.none()) row.instrument = "Drums";
        else if (part.programs.count() == 1 && !row.drums) {
            for (size_t i = 0; i < 128; ++i)
                if (part.programs[i]) row.instrument = midi::GeneralMidiNames[i];
        } else row.instrument = "Mixed instruments";
        for (size_t i = 0; i < 16; ++i) {
            if (!part.channels[i]) continue;
            if (!row.channels.empty()) row.channels += ", ";
            row.channels += std::to_string(i + 1);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}
}
