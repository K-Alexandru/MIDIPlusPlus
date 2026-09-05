#include "InputHeader.h"
#include <windows.h>

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
static UINT __fastcall SendInputCall(ULONG cInputs, LPINPUT pInputs, int cbSize)
{
    return ::SendInput(static_cast<UINT>(cInputs), pInputs, cbSize);
}

extern "C" UINT(__fastcall* InjectInput)(ULONG cInputs, LPINPUT pInputs, int cbSize) = SendInputCall;
