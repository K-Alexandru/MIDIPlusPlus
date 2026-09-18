#pragma once
#include <windows.h>

#ifdef __cplusplus
#include "InputLatency.hpp"
extern "C" {
#endif

	// Every injected keystroke goes through this pointer. It calls SendInput;
	// see InputInjector.cpp for why the direct syscall thunk was removed. The
	// indirection exists so tests can substitute an in-process recorder.
	extern UINT(__fastcall* InjectInput)(ULONG cInputs, LPINPUT pInputs, int cbSize);

#ifdef __cplusplus
}

// While Roblox is running, keys are typed only when it is in front: a tester
// tabbed out mid-song and the song went into the other application. Key ups
// still go out, so nothing the game was sent is left without its release.
// With no Roblox running nothing is held back, which is every other game.
bool IsRobloxImage(const wchar_t* imagePath) noexcept;
bool IsRobloxWindow(HWND window) noexcept;
// Copies the key ups from in to out, which must hold count, and returns how many.
UINT KeyUpsOnly(const INPUT* in, UINT count, INPUT* out) noexcept;
#endif
