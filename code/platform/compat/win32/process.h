// Linux port: stand-in for the MSVC header <process.h>.
// _beginthread and _beginthreadex are implemented with std::thread in
// code/platform/posix/win32_threads.cpp.
#pragma once

#include "../msvc_compat.h"

// Returns the thread handle, as _beginthread does on Windows. The handle stops being
// valid when the thread function returns; TerminateThread asks the thread to end.
std::uintptr_t _beginthread(void (*start)(void *), unsigned stackSize, void *argument);

// Returns the thread handle, which stays valid until CloseHandle, as on Windows.
// GetExitCodeThread reports the return value of the thread function once it has
// returned. The security attributes, stack size, creation flags and thread id are
// not supported (0 expected).
std::uintptr_t _beginthreadex(void *security, unsigned stackSize,
                              unsigned (__stdcall *start)(void *), void *argument,
                              unsigned initFlag, unsigned *threadId);
