/************************************************************************
    MeOS - Orienteering Software
    Linux port: threads started with _beginthread.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#pragma once

#include "win32_handle.h"

namespace meos_platform {

// Windows kills a thread inside TerminateThread. POSIX has no equivalent that is
// safe in C++, so the thread is asked to end instead: cancel() raises a flag and
// signals an eventfd. Every blocking wait in the platform layer also waits on that
// descriptor and then ends the thread with endCancelledThread(): the thread unwinds
// to its start function without returning to the caller of the wait. Returning a
// failure instead let MeOS go on with the thread, and SportIdent::MonitorSI then
// read the SportIdent object its terminator had already deleted (1.3.6).
class ThreadObject : public Win32Object {
public:
  ThreadObject();
  ~ThreadObject() override;

  void cancel();
  bool cancelled() const { return cancelRequested.load(); }
  int wakeDescriptor() const { return wakeEvent; }

  std::atomic<bool> finished{false};
  std::atomic<DWORD> exitCode{0};

private:
  std::atomic<bool> cancelRequested{false};
  int wakeEvent;
};

// The eventfd of the calling thread, or -1 for threads not started with _beginthread.
int currentThreadWakeDescriptor();

// Thrown by endCancelledThread and caught by the start function of the thread.
// MeOS code between may catch it with catch (...); it then has to end the thread.
struct ThreadTerminated {};

// Ends the calling thread after TerminateThread, at a blocking wait.
[[noreturn]] void endCancelledThread();

} // namespace meos_platform
