#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

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

// One folder of the scanned library, as the file list shows it while nothing
// is being searched for. The scan stays flat and names each file by its path
// below the chosen folder, so a folder is a prefix of those names: empty for
// the chosen folder itself, otherwise ending in a separator. A folder with no
// MIDI file anywhere below it is never listed, because nothing names it.
struct FolderView {
    std::vector<std::string> folders;
    std::vector<size_t> files;
};
inline FolderView BrowseFolder(const std::vector<MidiEntry>& files, const std::string& prefix) {
    FolderView view;
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& name = files[i].name;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        const auto cut = name.find_first_of("\\/", prefix.size());
        if (cut == std::string::npos) view.files.push_back(i);
        else view.folders.push_back(name.substr(prefix.size(), cut - prefix.size()));
    }
    const auto lower = [](std::string name) {
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return name;
    };
    std::sort(view.folders.begin(), view.folders.end(), [&](const auto& a, const auto& b) {
        const auto first = lower(a), second = lower(b);
        return first != second ? first < second : a < b;
    });
    view.folders.erase(std::unique(view.folders.begin(), view.folders.end()), view.folders.end());
    return view;
}
inline std::string ParentFolder(const std::string& prefix) {
    if (prefix.size() < 2) return {};
    const auto cut = prefix.find_last_of("\\/", prefix.size() - 2);
    return cut == std::string::npos ? std::string() : prefix.substr(0, cut + 1);
}
}
