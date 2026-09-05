#pragma once
#include <windows.h>

#ifdef __cplusplus
#include "InputLatency.hpp"
extern "C" {
#endif

	// Global syscall number (defined in implementation file)
	extern DWORD SyscallNumber;

	// Returns the NtUserSendInput syscall number
	unsigned long __cdecl GetNtUserSendInputSyscallNumber(void);

	// Direct function pointer - maximum speed
	// This replaces the regular function declaration for ultra-fast access
	extern UINT(__fastcall* NtUserSendInputCall)(ULONG cInputs, LPINPUT pInputs, int cbSize);

	// Initialize the direct syscall - call after setting SyscallNumber
	void InitializeNtUserSendInputCall(void);

	// Establishes an injection path and never throws. Prefers the direct
	// syscall and falls back to SendInput. Returns 1 when the syscall path is
	// active. Safe to call more than once.
	int __cdecl EnsureInputInjection(void);

	// 1 when the direct syscall is in use, 0 when injection goes through
	// SendInput. For reporting only: both paths inject.
	int __cdecl UsingSyscallInjection(void);

#ifdef __cplusplus
}
#endif
