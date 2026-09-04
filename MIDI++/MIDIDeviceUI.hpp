#pragma once
#include "PlaybackSystem.hpp"
#include "MidiInput.hpp"
#include <string>
#include <vector>

// The combo box and the code that opens a port now share one list, and
// selection is carried as a device id rather than a row number. Filtering or
// reordering the list can no longer make the app open a different device than
// the one on screen.
class MIDIDeviceUI {
public:
    static void PopulateMidiInDevices(HWND combo, std::wstring& selectedDeviceId);
    static void PopulateChannelList(HWND combo, int& selectedChannel);

    // Snapshot taken by the last PopulateMidiInDevices call.
    static const std::vector<MidiInputDevice>& Devices();
    static std::wstring DeviceIdAt(int comboIndex);
    static int IndexOfDeviceId(const std::wstring& deviceId);
    static bool IsDeviceAvailable(const std::wstring& deviceId);
};
