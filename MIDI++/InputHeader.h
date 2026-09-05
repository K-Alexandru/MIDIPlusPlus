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
#endif
