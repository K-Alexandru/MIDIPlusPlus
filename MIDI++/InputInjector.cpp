#include "InputHeader.h"
#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <iterator>

#pragma comment(lib, "shlwapi.lib")

// Keystroke injection goes through SendInput.
//
// This file used to assemble a thunk at run time: it read NtUserSendInput's
// syscall number out of win32u's prologue, wrote a syscall stub into an RWX
// page, and called that instead of SendInput. HANDOFF.md section 4 recorded
// that its speed benefit had never been measured.
//
// Measured 2026-09-05, on the four-input velocity tap the note path actually
// sends: the difference between the thunk and SendInput was smaller than the
// run-to-run noise and changed sign between runs (+1534, -659, +58, -18 ns
// against a call cost near 118us). With delivery removed so only the call
// itself was timed, the thunk saved 0 to 2 ns out of about 410. It bought
// nothing measurable.
//
// What it cost was real: any setup failure left a default that returned 69 and
// injected nothing, so keystrokes silently stopped reaching the game, and the
// process carried a page of hand-written syscall bytes for no gain.
//
// The indirection stays. InputLatency routes through it, and the tests
// substitute a recorder so no test keystroke reaches Windows.
bool IsRobloxImage(const wchar_t* imagePath) noexcept
{
    if (!imagePath || !*imagePath) return false;
    const wchar_t* name = imagePath;
    for (const wchar_t* p = imagePath; *p; ++p)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    if (_wcsnicmp(name, L"RobloxPlayer", 12) == 0) return true;
    // The Microsoft Store build runs under a generic name from its package folder.
    return _wcsicmp(name, L"Windows10Universal.exe") == 0 && StrStrIW(imagePath, L"ROBLOXCORPORATION");
}

UINT KeyUpsOnly(const INPUT* in, UINT count, INPUT* out) noexcept
{
    UINT kept = 0;
    for (UINT i = 0; i < count; ++i)
        if (in[i].type == INPUT_KEYBOARD && (in[i].ki.dwFlags & KEYEVENTF_KEYUP)) out[kept++] = in[i];
    return kept;
}

namespace {

bool IsRobloxWindow(HWND window) noexcept
{
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (!process) return false;
    const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    if (!handle) return false;
    wchar_t path[MAX_PATH * 2];
    DWORD length = static_cast<DWORD>(std::size(path));
    const bool roblox = QueryFullProcessImageNameW(handle, 0, path, &length) && IsRobloxImage(path);
    CloseHandle(handle);
    return roblox;
}

// This runs on every batch, on the thread that plays the note, so what costs
// anything is remembered: the verdict on the window in front until another
// window is, and whether Roblox is running at all for a second.
bool KeysMayBeTyped() noexcept
{
    constexpr uintptr_t IsRoblox = uintptr_t{1} << 63;
    static std::atomic<uintptr_t> front{0};
    const auto window = reinterpret_cast<uintptr_t>(GetForegroundWindow()) & ~IsRoblox;
    uintptr_t known = front.load(std::memory_order_relaxed);
    if ((known & ~IsRoblox) != window) {
        known = window | (IsRobloxWindow(reinterpret_cast<HWND>(window)) ? IsRoblox : 0);
        front.store(known, std::memory_order_relaxed);
    }
    if (known & IsRoblox) return true;

    static std::atomic<ULONGLONG> checked{0};
    static std::atomic<bool> running{false};
    const ULONGLONG now = GetTickCount64();
    if (now - checked.load(std::memory_order_relaxed) >= 1000) {
        bool found = false;
        for (HWND w = FindWindowExW(nullptr, nullptr, nullptr, L"Roblox"); w && !found;
             w = FindWindowExW(nullptr, w, nullptr, L"Roblox"))
            found = IsRobloxWindow(w);
        running.store(found, std::memory_order_relaxed);
        checked.store(now, std::memory_order_relaxed);
    }
    return !running.load(std::memory_order_relaxed);
}

} // namespace

static UINT __fastcall SendInputCall(ULONG cInputs, LPINPUT pInputs, int cbSize)
{
    if (KeysMayBeTyped() || !pInputs || cbSize != sizeof(INPUT))
        return ::SendInput(static_cast<UINT>(cInputs), pInputs, cbSize);

    // Held back is not failed: the caller is told the batch went, or it would
    // count a fault for every note played with the game behind.
    INPUT ups[64];
    for (ULONG done = 0; done < cInputs; done += static_cast<ULONG>(std::size(ups))) {
        const UINT chunk = static_cast<UINT>((std::min)(cInputs - done, static_cast<ULONG>(std::size(ups))));
        const UINT kept = KeyUpsOnly(pInputs + done, chunk, ups);
        if (kept) ::SendInput(kept, ups, sizeof(INPUT));
    }
    return static_cast<UINT>(cInputs);
}

extern "C" UINT(__fastcall* InjectInput)(ULONG cInputs, LPINPUT pInputs, int cbSize) = SendInputCall;
