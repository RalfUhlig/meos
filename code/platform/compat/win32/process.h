// Linux port: stand-in for the MSVC header <process.h>.
// _beginthread is implemented with std::thread in code/platform/posix/win32_threads.cpp.
#pragma once

#include "../msvc_compat.h"

// Returns the thread handle, as _beginthread does on Windows. The handle stops being
// valid when the thread function returns; TerminateThread asks the thread to end.
std::uintptr_t _beginthread(void (*start)(void *), unsigned stackSize, void *argument);
