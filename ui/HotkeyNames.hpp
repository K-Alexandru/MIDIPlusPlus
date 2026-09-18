#pragma once
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace shell {
// Config names are the upstream "VK_F1" spelling. Unknown names register
// nothing rather than guessing at a keycode.
inline int NameToVK(std::string name) {
    if (name.rfind("VK_", 0) == 0) name.erase(0, 3);
    if (name.empty()) return 0;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (name.size() == 1 && (std::isalnum(static_cast<unsigned char>(name[0])) != 0))
        return static_cast<unsigned char>(name[0]);
    if (name[0] == 'F' && name.size() <= 3) {
        const int number = std::atoi(name.c_str() + 1);
        if (number >= 1 && number <= 24) return VK_F1 + number - 1;
    }
    static const std::unordered_map<std::string, int> named{
        {"SPACE", VK_SPACE}, {"TAB", VK_TAB},     {"PAUSE", VK_PAUSE},
        {"LEFT", VK_LEFT},   {"RIGHT", VK_RIGHT}, {"UP", VK_UP},
        {"DOWN", VK_DOWN},   {"HOME", VK_HOME},   {"END", VK_END},
        {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
        {"PRIOR", VK_PRIOR}, {"NEXT", VK_NEXT},
        {"MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE}, {"MEDIA_STOP", VK_MEDIA_STOP},
        {"MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK}, {"MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK},
    };
    const auto found = named.find(name);
    return found == named.end() ? 0 : found->second;
}
}
