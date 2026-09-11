/************************************************************************
    MeOS - Orienteering Software
    Linux port: kernel objects behind Win32 HANDLE values.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#pragma once

#include "windows.h"

namespace meos_platform {

// Base class of every object a HANDLE created by code/platform/posix refers to.
// CloseHandle (or FindClose) deletes the object.
class Win32Object {
public:
  virtual ~Win32Object() = default;
};

inline HANDLE toHandle(Win32Object *object) {
  return static_cast<HANDLE>(object);
}

template<typename T>
T *fromHandle(HANDLE handle) {
  if (!handle || handle == INVALID_HANDLE_VALUE)
    return nullptr;
  return dynamic_cast<T *>(static_cast<Win32Object *>(handle));
}

// Maps an errno value to the closest Win32 error code.
DWORD win32ErrorFromErrno(int error);

} // namespace meos_platform
