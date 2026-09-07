#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace shell {
struct GameWindow {
    uintptr_t id = 0;
    uint32_t process = 0;
    std::string title;
};

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
