#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Original synthetic score: conductor, two piano parts, flute, strings, drums.
// Deliberately includes a program change in the conductor track and velocity-0
// note-offs. No downloaded music is needed by these tests.
inline void WriteTrackFixture(const std::filesystem::path& path) {
    using Bytes = std::vector<uint8_t>;
    Bytes file{'M','T','h','d',0,0,0,6,0,1,0,6,1,0xE0};
    const auto track = [&](Bytes data) {
        data.insert(data.end(), {0,0xFF,0x2F,0});
        file.insert(file.end(), {'M','T','r','k'});
        const auto size = static_cast<uint32_t>(data.size());
        for (int i = 3; i >= 0; --i) file.push_back(static_cast<uint8_t>(size >> (i * 8)));
        file.insert(file.end(), data.begin(), data.end());
    };
    track({0,0xC2,73, 0,0xFF,0x51,3,7,0xA1,0x20});
    const char* names[] = {"Piano R.H.","Piano L.H.","Flute","Strings","Drums"};
    const uint8_t channels[] = {0,1,2,3,9};
    const uint8_t programs[] = {0,0,73,48,0};
    for (int i = 0; i < 5; ++i) {
        const std::string name(names[i]);
        Bytes data{0,0xFF,3,static_cast<uint8_t>(name.size())};
        data.insert(data.end(), name.begin(), name.end());
        if (i != 2) data.insert(data.end(), {0,static_cast<uint8_t>(0xC0 | channels[i]),programs[i]});
        data.insert(data.end(), {0,static_cast<uint8_t>(0x90 | channels[i]),static_cast<uint8_t>(60 + i),80,
                                0x83,0x60,static_cast<uint8_t>(0x90 | channels[i]),static_cast<uint8_t>(60 + i),0});
        track(std::move(data));
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out) throw std::runtime_error("Could not write synthetic MIDI fixture");
}
