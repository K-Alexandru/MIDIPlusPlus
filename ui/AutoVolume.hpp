#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace shell {
struct GameWindow {
    uintptr_t id = 0;
    uint32_t process = 0;
    std::string title;
    // A window the app knows to be a piano game. It leads the list and is the
    // one already chosen when AutoVol opens: a tester found every open
    // application listed and Roblox, the one they wanted, somewhere among them.
    bool game = false;
};

// Known games first, then by title.
inline void OrderGameWindows(std::vector<GameWindow>& windows) {
    std::stable_sort(windows.begin(), windows.end(), [](const GameWindow& a, const GameWindow& b) {
        return a.game != b.game ? a.game : a.title < b.title;
    });
}

// Window operations are separate from calibration so verification can record
// injection without focusing or sending input to another application.
class AutoVolumeHost {
public:
    virtual ~AutoVolumeHost() = default;
    virtual std::vector<GameWindow> Windows() = 0;
    virtual bool Focus(const GameWindow& window) = 0;
    virtual bool IsForeground(const GameWindow& window) = 0;
};
}
