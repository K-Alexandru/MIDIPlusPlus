#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace shell {
struct MidiEntry {
    std::filesystem::path path;
    std::string name;
    uintmax_t bytes = 0;
    std::filesystem::file_time_type modified{};
};
enum class FileSort { Name, Size, Modified };
inline bool FileBefore(const MidiEntry& a, const MidiEntry& b, FileSort sort, bool descending) {
    const auto lower = [](std::string name) {
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return name;
    };
    int order = 0;
    if (sort == FileSort::Size && a.bytes != b.bytes) order = a.bytes < b.bytes ? -1 : 1;
    else if (sort == FileSort::Modified && a.modified != b.modified) order = a.modified < b.modified ? -1 : 1;
    else {
        const auto first = lower(a.name), second = lower(b.name);
        if (first != second) order = first < second ? -1 : 1;
        else if (a.path != b.path) order = a.path < b.path ? -1 : 1;
    }
    return descending ? order > 0 : order < 0;
}
}
