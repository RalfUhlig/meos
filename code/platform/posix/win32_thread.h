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
// descriptor, returns a failure, and the thread function unwinds on its own.
class ThreadObject : public Win32Object {
public:
  ThreadObject();
  ~ThreadObject() override;

  void cancel();
  bool cancelled() const { return cancelRequested.load(); }
  int wakeDescriptor() const { return wakeEvent; }

  std::atomic<bool> finished{false};

private:
  std::atomic<bool> cancelRequested{false};
  int wakeEvent;
};

// The eventfd of the calling thread, or -1 for threads not started with _beginthread.
int currentThreadWakeDescriptor();

} // namespace meos_platform
