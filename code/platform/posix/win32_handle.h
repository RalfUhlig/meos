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
class Win32Object {
public:
  virtual ~Win32Object() = default;
};

// A file descriptor behind a HANDLE. Serial ports extend it in win32_serial.cpp.
class FileObject : public Win32Object {
public:
  explicit FileObject(int fd) : fd(fd) {}
  ~FileObject() override;
  const int fd;
};

// The handle table keeps the object alive until CloseHandle. A caller that is
// working with an object holds its own reference, so closing a handle in one
// thread cannot take the file descriptor away from a blocking read in another.
HANDLE registerObject(std::shared_ptr<Win32Object> object);
std::shared_ptr<Win32Object> lookupObject(HANDLE handle);
bool unregisterObject(HANDLE handle);

template<typename T>
std::shared_ptr<T> fromHandle(HANDLE handle) {
  return std::dynamic_pointer_cast<T>(lookupObject(handle));
}

// Maps an errno value to the closest Win32 error code.
DWORD win32ErrorFromErrno(int error);

// Opens a serial port, given either a Windows device name (\\.\COM3, //./COM3) or a
// POSIX device path. Returns nullptr and sets the last error if that fails.
std::shared_ptr<Win32Object> openSerialPort(const std::string &name, bool read, bool write);

// Wraps an already open terminal (used by CreateFile for /dev/tty* paths).
std::shared_ptr<Win32Object> makeSerialObject(int fd);

} // namespace meos_platform
