#pragma once
#include <windows.h>

// Modeless diagnostics in the current Win32 shell. Closing removes the hook.
void ShowInputLatencyWindow(HWND owner);

