// Linux port: stand-in for the MSVC header <io.h> (low-level file I/O).
#pragma once

#include "../msvc_compat.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _O_RDWR   O_RDWR
#define _O_APPEND O_APPEND
#define _O_CREAT  O_CREAT
#define _O_TRUNC  O_TRUNC
#define _O_EXCL   O_EXCL
#define _O_BINARY 0
#define _O_TEXT   0

// Windows share modes have no POSIX equivalent at open(); they are ignored.
// Concurrent access protection, where required, must use explicit locking.
#define _SH_DENYRW 0x10
#define _SH_DENYWR 0x20
#define _SH_DENYRD 0x30
#define _SH_DENYNO 0x40

#define _S_IREAD  (S_IRUSR | S_IRGRP | S_IROTH)
#define _S_IWRITE (S_IWUSR | S_IWGRP | S_IWOTH)

inline errno_t _wsopen_s(int *fd, const wchar_t *file, int oflag, int /*shflag*/, int pmode) {
  if (!fd)
    return EINVAL;
  *fd = ::open(meos_compat::nativePath(file).c_str(), oflag, pmode);
  return *fd == -1 ? errno : 0;
}

inline int _open(const char *file, int oflag, int pmode = 0) {
  return ::open(file, oflag, pmode);
}

inline int _read(int fd, void *buffer, unsigned int count) {
  return static_cast<int>(::read(fd, buffer, count));
}

inline int _write(int fd, const void *buffer, unsigned int count) {
  return static_cast<int>(::write(fd, buffer, count));
}

inline int _close(int fd) {
  return ::close(fd);
}

inline long _lseek(int fd, long offset, int origin) {
  return static_cast<long>(::lseek(fd, offset, origin));
}

inline long _filelength(int fd) {
  struct stat st;
  return ::fstat(fd, &st) == 0 ? static_cast<long>(st.st_size) : -1L;
}
