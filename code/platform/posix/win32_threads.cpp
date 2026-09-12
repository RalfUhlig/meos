/************************************************************************
    MeOS - Orienteering Software
    Linux port: Win32 threads, critical sections and Sleep.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_thread.h"

#include "process.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace {

thread_local std::shared_ptr<meos_platform::ThreadObject> currentThread;

std::recursive_mutex *mutexOf(CRITICAL_SECTION *section) {
  return static_cast<std::recursive_mutex *>(section->DebugInfo);
}

} // namespace

meos_platform::ThreadObject::ThreadObject() : wakeEvent(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {}

meos_platform::ThreadObject::~ThreadObject() {
  if (wakeEvent >= 0)
    ::close(wakeEvent);
}

void meos_platform::ThreadObject::cancel() {
  cancelRequested = true;
  // Stays readable: every later wait in this thread ends at once as well.
  const std::uint64_t one = 1;
  if (wakeEvent >= 0)
    (void)::write(wakeEvent, &one, sizeof(one));
}

int meos_platform::currentThreadWakeDescriptor() {
  return currentThread ? currentThread->wakeDescriptor() : -1;
}

std::uintptr_t _beginthread(void (*start)(void *), unsigned /*stackSize*/, void *argument) {
  auto thread = std::make_shared<meos_platform::ThreadObject>();
  const HANDLE handle = meos_platform::registerObject(thread);

  std::thread([thread, start, argument, handle] {
    currentThread = thread;
    start(argument);
    thread->finished = true;
    currentThread.reset();
    // As with _beginthread on Windows, the handle stops being valid here.
    meos_platform::unregisterObject(handle);
  }).detach();

  return reinterpret_cast<std::uintptr_t>(handle);
}

// Asks the thread to end. Unlike on Windows this returns before the thread has
// ended: MeOS calls TerminateThread while holding a critical section that the
// thread itself needs, so waiting here would deadlock.
BOOL TerminateThread(HANDLE thread, DWORD /*exitCode*/) {
  const std::shared_ptr<meos_platform::ThreadObject> object =
      meos_platform::fromHandle<meos_platform::ThreadObject>(thread);
  if (!object) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  object->cancel();
  return TRUE;
}

BOOL GetExitCodeThread(HANDLE thread, LPDWORD exitCode) {
  const std::shared_ptr<meos_platform::ThreadObject> object =
      meos_platform::fromHandle<meos_platform::ThreadObject>(thread);
  if (!object || !exitCode) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  *exitCode = object->finished ? 0 : STILL_ACTIVE;
  return TRUE;
}

void Sleep(DWORD milliseconds) {
  if (milliseconds == 0) {
    std::this_thread::yield();
    return;
  }

  const int wake = meos_platform::currentThreadWakeDescriptor();
  const std::uint64_t deadline = GetTickCount64() + milliseconds;
  for (;;) {
    const std::uint64_t now = GetTickCount64();
    if (now >= deadline)
      return;
    pollfd waited = {wake, POLLIN, 0};
    const int ready = ::poll(wake >= 0 ? &waited : nullptr, wake >= 0 ? 1 : 0,
                             static_cast<int>(deadline - now));
    if (ready != 0 && !(ready < 0 && errno == EINTR))
      return; // The thread was asked to end, or poll failed.
  }
}

void InitializeCriticalSection(CRITICAL_SECTION *section) {
  std::memset(section, 0, sizeof(*section));
  // Windows critical sections may be entered again by the thread that owns them.
  section->DebugInfo = new std::recursive_mutex;
}

void DeleteCriticalSection(CRITICAL_SECTION *section) {
  delete mutexOf(section);
  section->DebugInfo = nullptr;
}

void EnterCriticalSection(CRITICAL_SECTION *section) {
  mutexOf(section)->lock();
}

void LeaveCriticalSection(CRITICAL_SECTION *section) {
  mutexOf(section)->unlock();
}
