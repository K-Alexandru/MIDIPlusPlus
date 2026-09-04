#include "MIDIDeviceUI.hpp"
#include <mmsystem.h>

namespace {
    // Whatever the combo currently shows, in the same order.
    std::vector<MidiInputDevice> g_devices;
}

const std::vector<MidiInputDevice>& MIDIDeviceUI::Devices() {
    return g_devices;
}

std::wstring MIDIDeviceUI::DeviceIdAt(int comboIndex) {
    if (comboIndex < 0 || comboIndex >= static_cast<int>(g_devices.size())) return {};
    return g_devices[static_cast<size_t>(comboIndex)].id;
}

int MIDIDeviceUI::IndexOfDeviceId(const std::wstring& deviceId) {
    for (size_t i = 0; i < g_devices.size(); ++i) {
        if (g_devices[i].id == deviceId) return static_cast<int>(i);
    }
    return -1;
}

bool MIDIDeviceUI::IsDeviceAvailable(const std::wstring& deviceId) {
    return IndexOfDeviceId(deviceId) >= 0;
}

void MIDIDeviceUI::PopulateMidiInDevices(HWND combo, std::wstring& selectedDeviceId) {
    SendMessage(combo, CB_RESETCONTENT, 0, 0);

    // No access test and no filtering: a device that fails to open reports it
    // when it is opened, and dropping rows here is what made the combo index
    // disagree with every other index space in the app.
    g_devices = EnumerateMidiInputs();

    if (g_devices.empty()) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"No Devices");
        SendMessage(combo, CB_SETCURSEL, 0, 0);
        selectedDeviceId.clear();
        return;
    }

    for (const auto& device : g_devices) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)device.name.c_str());
    }

    int index = selectedDeviceId.empty() ? -1 : IndexOfDeviceId(selectedDeviceId);
    if (index < 0) {
        index = 0;
        selectedDeviceId = g_devices[0].id;
    }
    SendMessage(combo, CB_SETCURSEL, index, 0);
}

void MIDIDeviceUI::PopulateChannelList(HWND combo, int& selectedChannel) {
    SendMessage(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"All Channels");

    for (int ch = 0; ch < 16; ch++) {
        wchar_t buf[32];
        swprintf_s(buf, L"Channel %d", ch);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)buf);
    }

    SendMessage(combo, CB_SETCURSEL, 0, 0);
    selectedChannel = -1;
}
