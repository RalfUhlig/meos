/************************************************************************
    MeOS - Orienteering Software
    Linux port: Win32 serial ports (SI readers) on termios.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// MeOS identifies a serial port by a COM number throughout: QueryDosDevice offers
// the numbers, the user picks one, and CreateFile(L"//./COM<n>") opens it. Linux
// has device paths instead, so a number denotes a device by this fixed scheme:
//
//   COM1 - COM32   /dev/ttyUSB0 ...  USB serial adapters - the SI master stations
//                                    (Silicon Labs cp210x, FTDI ftdi_sio)
//   COM33 - COM64  /dev/ttyACM0 ...  USB CDC devices
//   COM65 - COM96  /dev/ttyS0 ...    built-in UARTs
//
// The mapping does not depend on what is plugged in, so a port selected for a
// competition still means the same device later. A device path (/dev/ttyUSB0) may
// also be given instead of a COM name.

#include "win32_thread.h"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// MeOS sets DCBlength to sizeof(DCB) and passes the structure on unchanged.
static_assert(sizeof(DCB) == 28, "DCB differs from the Windows layout");

namespace {

struct PortFamily {
  const char *prefix;
  int firstComNumber;
  int count;
};

constexpr PortFamily portFamilies[] = {
  {"ttyUSB", 1, 32},
  {"ttyACM", 33, 32},
  {"ttyS", 65, 32},
};

class SerialObject : public meos_platform::FileObject {
public:
  explicit SerialObject(int fd) : FileObject(fd) {}

  DCB state{};
  COMMTIMEOUTS timeouts{};
  DWORD eventMask = 0;
};

std::shared_ptr<SerialObject> serialFromHandle(HANDLE handle) {
  const std::shared_ptr<SerialObject> port = meos_platform::fromHandle<SerialObject>(handle);
  if (!port)
    SetLastError(ERROR_INVALID_HANDLE);
  return port;
}

// "COM3" -> "/dev/ttyUSB2", empty for a number outside the scheme above.
std::string devicePathOfComName(const std::string &name) {
  if (name.size() < 4 || ::strncasecmp(name.c_str(), "COM", 3) != 0)
    return std::string();
  const std::string digits = name.substr(3);
  if (digits.find_first_not_of("0123456789") != std::string::npos)
    return std::string();

  const int number = std::atoi(digits.c_str());
  for (const PortFamily &family : portFamilies) {
    const int index = number - family.firstComNumber;
    if (index >= 0 && index < family.count)
      return std::string("/dev/") + family.prefix + std::to_string(index);
  }
  return std::string();
}

// The reverse direction, for QueryDosDevice. Returns 0 for a name outside the scheme.
int comNumberOfDeviceName(const std::string &name) {
  for (const PortFamily &family : portFamilies) {
    const std::size_t prefixLength = std::strlen(family.prefix);
    if (name.compare(0, prefixLength, family.prefix) != 0)
      continue;
    const std::string digits = name.substr(prefixLength);
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos)
      continue;
    const int index = std::atoi(digits.c_str());
    if (index < family.count)
      return family.firstComNumber + index;
  }
  return 0;
}

// Linux creates /dev/ttyS0 to /dev/ttyS31 whether or not there is a UART behind them.
// Only ports that a driver has bound are real.
bool deviceExists(const std::string &name) {
  if (::access(("/dev/" + name).c_str(), F_OK) != 0)
    return false;
  if (name.compare(0, 4, "ttyS") != 0)
    return true;
  return ::access(("/sys/class/tty/" + name + "/device").c_str(), F_OK) == 0;
}

std::vector<int> availablePorts() {
  std::vector<int> ports;
  DIR *dev = ::opendir("/dev");
  if (!dev)
    return ports;

  while (dirent *entry = ::readdir(dev)) {
    const int number = comNumberOfDeviceName(entry->d_name);
    if (number > 0 && deviceExists(entry->d_name))
      ports.push_back(number);
  }
  ::closedir(dev);
  std::sort(ports.begin(), ports.end());
  return ports;
}

speed_t baudConstant(DWORD baudRate) {
  switch (baudRate) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default: return 0;
  }
}

// The line settings CreateFile gives a COM port on Windows: raw 8N1, no flow control.
bool configureRawMode(int fd) {
  termios settings;
  if (::tcgetattr(fd, &settings) != 0)
    return false;

  ::cfmakeraw(&settings);
  settings.c_cflag |= CLOCAL | CREAD;
  settings.c_cflag &= ~CRTSCTS;
  // Reads are driven by poll() in ReadFile, which implements the Windows timeouts.
  settings.c_cc[VMIN] = 0;
  settings.c_cc[VTIME] = 0;
  return ::tcsetattr(fd, TCSANOW, &settings) == 0;
}

bool applyState(int fd, const DCB &state) {
  termios settings;
  if (::tcgetattr(fd, &settings) != 0)
    return false;

  const speed_t speed = baudConstant(state.BaudRate);
  if (speed == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }
  ::cfsetispeed(&settings, speed);
  ::cfsetospeed(&settings, speed);

  settings.c_cflag &= ~CSIZE;
  switch (state.ByteSize) {
    case 5: settings.c_cflag |= CS5; break;
    case 6: settings.c_cflag |= CS6; break;
    case 7: settings.c_cflag |= CS7; break;
    default: settings.c_cflag |= CS8; break;
  }

  settings.c_cflag &= ~(PARENB | PARODD);
  if (state.Parity == ODDPARITY)
    settings.c_cflag |= PARENB | PARODD;
  else if (state.Parity == EVENPARITY)
    settings.c_cflag |= PARENB;

  if (state.StopBits == TWOSTOPBITS)
    settings.c_cflag |= CSTOPB;
  else
    settings.c_cflag &= ~CSTOPB;

  if (::tcsetattr(fd, TCSANOW, &settings) != 0) {
    SetLastError(meos_platform::win32ErrorFromErrno(errno));
    return false;
  }

  // MeOS disables both lines; a master station is powered over USB and does not
  // use hardware handshaking.
  int lines = 0;
  if (state.fDtrControl != DTR_CONTROL_DISABLE)
    lines |= TIOCM_DTR;
  if (state.fRtsControl != RTS_CONTROL_DISABLE)
    lines |= TIOCM_RTS;
  int clear = (TIOCM_DTR | TIOCM_RTS) & ~lines;
  if (lines)
    ::ioctl(fd, TIOCMBIS, &lines);
  if (clear)
    ::ioctl(fd, TIOCMBIC, &clear);
  return true;
}

// Milliseconds until the earlier of the two deadlines, or -1 if neither is set.
// A deadline of 0 means "not set"; a deadline that has passed gives 0.
int millisecondsLeft(std::uint64_t totalDeadline, std::uint64_t intervalDeadline) {
  std::uint64_t deadline = totalDeadline;
  if (intervalDeadline && (!deadline || intervalDeadline < deadline))
    deadline = intervalDeadline;
  if (!deadline)
    return -1;
  const std::uint64_t now = GetTickCount64();
  return now >= deadline ? 0 : static_cast<int>(deadline - now);
}

enum class WaitResult { Ready, TimedOut, Cancelled, Failed };

// Waits for the port to become readable or writable, and at the same time for the
// request to end this thread (see win32_thread.h).
WaitResult waitFor(int fd, short events, int timeout) {
  const int wake = meos_platform::currentThreadWakeDescriptor();
  pollfd waited[2] = {{fd, events, 0}, {wake, POLLIN, 0}};
  const nfds_t count = wake >= 0 ? 2 : 1;
  const std::uint64_t deadline = timeout < 0 ? 0 : GetTickCount64() + timeout;

  for (;;) {
    const int ready = ::poll(waited, count, timeout);
    if (ready == 0)
      return WaitResult::TimedOut;
    if (ready < 0) {
      if (errno == EINTR) {
        // A signal, not a timeout: wait again, but not longer than agreed.
        timeout = millisecondsLeft(deadline, 0);
        if (timeout == 0)
          return WaitResult::TimedOut;
        continue;
      }
      SetLastError(meos_platform::win32ErrorFromErrno(errno));
      return WaitResult::Failed;
    }
    if (count == 2 && waited[1].revents) {
      SetLastError(ERROR_OPERATION_ABORTED);
      return WaitResult::Cancelled;
    }
    if (waited[0].revents & events)
      return WaitResult::Ready;
    // POLLHUP or POLLERR without data: the device is gone (adapter unplugged).
    SetLastError(ERROR_OPERATION_ABORTED);
    return WaitResult::Failed;
  }
}

} // namespace

std::shared_ptr<meos_platform::Win32Object> meos_platform::makeSerialObject(int fd) {
  configureRawMode(fd);
  return std::make_shared<SerialObject>(fd);
}

std::shared_ptr<meos_platform::Win32Object> meos_platform::openSerialPort(const std::string &name,
                                                                         bool read, bool write) {
  std::string path = name;
  if (path.compare(0, 5, "/dev/") != 0) {
    path = devicePathOfComName(name);
    if (path.empty()) {
      SetLastError(ERROR_FILE_NOT_FOUND);
      return nullptr;
    }
  }

  const int mode = read && write ? O_RDWR : write ? O_WRONLY : O_RDONLY;
  const int fd = ::open(path.c_str(), mode | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    SetLastError(win32ErrorFromErrno(errno));
    return nullptr;
  }
  if (!::isatty(fd) || !configureRawMode(fd)) {
    ::close(fd);
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  SetLastError(ERROR_SUCCESS);
  return std::make_shared<SerialObject>(fd);
}

// Reads until the buffer is full or a timeout expires. As on Windows an expired
// timeout is not an error: the call succeeds with the bytes read so far.
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD count, LPDWORD read, LPVOID /*overlapped*/) {
  if (read)
    *read = 0;

  const std::shared_ptr<meos_platform::FileObject> object =
      meos_platform::fromHandle<meos_platform::FileObject>(file);
  if (!object || !buffer) {
    SetLastError(object ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
    return FALSE;
  }
  const SerialObject *port = dynamic_cast<const SerialObject *>(object.get());

  const std::uint64_t now = GetTickCount64();
  std::uint64_t totalDeadline = 0;
  DWORD interval = 0;
  if (port) {
    const DWORD total = port->timeouts.ReadTotalTimeoutMultiplier * count +
                        port->timeouts.ReadTotalTimeoutConstant;
    totalDeadline = total ? now + total : 0;
    interval = port->timeouts.ReadIntervalTimeout;
  }

  BYTE *out = static_cast<BYTE *>(buffer);
  DWORD got = 0;
  std::uint64_t intervalDeadline = 0;
  while (got < count) {
    const ssize_t bytes = ::read(object->fd, out + got, count - got);
    if (bytes > 0) {
      got += static_cast<DWORD>(bytes);
      if (interval)
        intervalDeadline = GetTickCount64() + interval;
      continue;
    }
    // A terminal with VMIN = 0 answers "nothing there yet" with zero bytes rather
    // than EAGAIN; on a regular file the same answer means end of file. A port that
    // has gone away (adapter unplugged) shows up as POLLHUP in waitFor below.
    if (bytes == 0 && !port)
      break;
    if (bytes < 0) {
      if (errno == EINTR)
        continue;
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        SetLastError(meos_platform::win32ErrorFromErrno(errno));
        return FALSE;
      }
    }

    const int timeout = millisecondsLeft(totalDeadline, intervalDeadline);
    if (timeout == 0)
      break;
    const WaitResult result = waitFor(object->fd, POLLIN, timeout);
    if (result == WaitResult::TimedOut)
      break;
    if (result != WaitResult::Ready)
      return FALSE;
  }

  if (read)
    *read = got;
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD count, LPDWORD written, LPVOID /*overlapped*/) {
  if (written)
    *written = 0;

  const std::shared_ptr<meos_platform::FileObject> object =
      meos_platform::fromHandle<meos_platform::FileObject>(file);
  if (!object || !buffer) {
    SetLastError(object ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
    return FALSE;
  }
  const SerialObject *port = dynamic_cast<const SerialObject *>(object.get());

  std::uint64_t deadline = 0;
  if (port) {
    const DWORD total = port->timeouts.WriteTotalTimeoutMultiplier * count +
                        port->timeouts.WriteTotalTimeoutConstant;
    if (total)
      deadline = GetTickCount64() + total;
  }

  const BYTE *in = static_cast<const BYTE *>(buffer);
  DWORD done = 0;
  while (done < count) {
    const ssize_t bytes = ::write(object->fd, in + done, count - done);
    if (bytes > 0) {
      done += static_cast<DWORD>(bytes);
      continue;
    }
    if (bytes < 0 && errno == EINTR)
      continue;
    if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      SetLastError(meos_platform::win32ErrorFromErrno(errno));
      return FALSE;
    }

    const int timeout = millisecondsLeft(deadline, 0);
    if (timeout == 0)
      break;
    const WaitResult result = waitFor(object->fd, POLLOUT, timeout);
    if (result == WaitResult::TimedOut)
      break;
    if (result != WaitResult::Ready)
      return FALSE;
  }

  if (written)
    *written = done;
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL GetCommState(HANDLE file, LPDCB state) {
  const std::shared_ptr<SerialObject> port = serialFromHandle(file);
  if (!port || !state)
    return FALSE;
  *state = port->state;
  state->DCBlength = sizeof(DCB);
  return TRUE;
}

BOOL SetCommState(HANDLE file, LPDCB state) {
  const std::shared_ptr<SerialObject> port = serialFromHandle(file);
  if (!port || !state)
    return FALSE;
  if (!applyState(port->fd, *state))
    return FALSE;
  port->state = *state;
  return TRUE;
}

BOOL GetCommTimeouts(HANDLE file, COMMTIMEOUTS *timeouts) {
  const std::shared_ptr<SerialObject> port = serialFromHandle(file);
  if (!port || !timeouts)
    return FALSE;
  *timeouts = port->timeouts;
  return TRUE;
}

BOOL SetCommTimeouts(HANDLE file, COMMTIMEOUTS *timeouts) {
  const std::shared_ptr<SerialObject> port = serialFromHandle(file);
  if (!port || !timeouts)
    return FALSE;
  port->timeouts = *timeouts;
  return TRUE;
}

BOOL SetCommMask(HANDLE file, DWORD mask) {
  const std::shared_ptr<SerialObject> port = serialFromHandle(file);
  if (!port)
    return FALSE;
  port->eventMask = mask;
  return TRUE;
}

// Waits for an incoming character, the only event MeOS asks for.
BOOL WaitCommEvent(HANDLE file, LPDWORD mask, LPVOID /*overlapped*/) {
  if (mask)
    *mask = 0;
  const std::shared_ptr<SerialObject> port = serialFromHandle(file);
  if (!port)
    return FALSE;
  if (!(port->eventMask & EV_RXCHAR)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  if (waitFor(port->fd, POLLIN, -1) != WaitResult::Ready)
    return FALSE;
  if (mask)
    *mask = EV_RXCHAR;
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

// Only the Windows form used by MeOS is supported: all device names at once, as a
// list of strings with a second null character behind the last one.
DWORD QueryDosDevice(LPCWSTR deviceName, LPWSTR targetPath, DWORD max) {
  if (deviceName) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }

  std::wstring names;
  for (int port : availablePorts()) {
    names += L"COM" + std::to_wstring(port);
    names += L'\0';
  }
  if (names.empty())
    return 0;
  names += L'\0';

  if (!targetPath || max < names.size()) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  std::wmemcpy(targetPath, names.c_str(), names.size());
  SetLastError(ERROR_SUCCESS);
  return static_cast<DWORD>(names.size());
}
