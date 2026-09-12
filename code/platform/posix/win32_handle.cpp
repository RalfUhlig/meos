/************************************************************************
    MeOS - Orienteering Software
    Linux port: the handle table behind Win32 HANDLE values.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_handle.h"

#include <unordered_map>
#include <unistd.h>

namespace {

std::mutex tableMutex;
std::unordered_map<HANDLE, std::shared_ptr<meos_platform::Win32Object>> table;

} // namespace

meos_platform::FileObject::~FileObject() {
  ::close(fd);
}

HANDLE meos_platform::registerObject(std::shared_ptr<Win32Object> object) {
  HANDLE handle = static_cast<HANDLE>(object.get());
  std::lock_guard<std::mutex> lock(tableMutex);
  table[handle] = std::move(object);
  return handle;
}

std::shared_ptr<meos_platform::Win32Object> meos_platform::lookupObject(HANDLE handle) {
  if (!handle || handle == INVALID_HANDLE_VALUE)
    return nullptr;
  std::lock_guard<std::mutex> lock(tableMutex);
  const auto entry = table.find(handle);
  return entry == table.end() ? nullptr : entry->second;
}

bool meos_platform::unregisterObject(HANDLE handle) {
  if (!handle || handle == INVALID_HANDLE_VALUE)
    return false;
  std::shared_ptr<Win32Object> object;
  {
    std::lock_guard<std::mutex> lock(tableMutex);
    const auto entry = table.find(handle);
    if (entry == table.end())
      return false;
    object = std::move(entry->second);
    table.erase(entry);
  }
  // The object is destroyed here, outside the lock, unless another thread still uses it.
  return true;
}

BOOL CloseHandle(HANDLE handle) {
  if (!meos_platform::unregisterObject(handle)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return TRUE;
}
