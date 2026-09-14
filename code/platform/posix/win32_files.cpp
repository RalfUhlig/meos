/************************************************************************
    MeOS - Orienteering Software
    Linux port: Win32 file system, error and system information functions.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Paths may use '\' or '/' as separator. Windows share modes are not enforced,
// and file name matching is case-insensitive as on Windows file systems.

#include "win32_handle.h"

#include <cctype>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

thread_local DWORD lastError = ERROR_SUCCESS;

// 100-nanosecond intervals between 1601-01-01 (FILETIME epoch) and 1970-01-01.
constexpr std::int64_t fileTimeUnixEpoch = 116444736000000000LL;

FILETIME toFileTime(const timespec &ts) {
  const std::uint64_t ticks = static_cast<std::uint64_t>(
      static_cast<std::int64_t>(ts.tv_sec) * 10000000LL + ts.tv_nsec / 100 + fileTimeUnixEpoch);
  FILETIME ft;
  ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
  ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
  return ft;
}

DWORD attributesOf(const std::string &path, const std::string &name, const struct stat &st) {
  DWORD attributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
  if (::access(path.c_str(), W_OK) != 0)
    attributes |= FILE_ATTRIBUTE_READONLY;
  if (name.size() > 1 && name[0] == '.' && name != "..")
    attributes |= FILE_ATTRIBUTE_HIDDEN;
  return attributes;
}

class FindHandle : public meos_platform::Win32Object {
public:
  ~FindHandle() override {
    if (dir)
      ::closedir(dir);
  }
  DIR *dir = nullptr;
  std::string directory;
  std::string pattern;
};

bool findNextMatch(FindHandle &find, LPWIN32_FIND_DATA data) {
  while (dirent *entry = ::readdir(find.dir)) {
    if (::fnmatch(find.pattern.c_str(), entry->d_name, FNM_CASEFOLD) != 0)
      continue;

    const std::string name = entry->d_name;
    const std::string path = find.directory + "/" + name;
    std::memset(data, 0, sizeof(*data));
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
      data->dwFileAttributes = attributesOf(path, name, st);
      data->ftCreationTime = toFileTime(st.st_mtim);
      data->ftLastAccessTime = toFileTime(st.st_atim);
      data->ftLastWriteTime = toFileTime(st.st_mtim);
      const std::uint64_t size = S_ISDIR(st.st_mode) ? 0 : static_cast<std::uint64_t>(st.st_size);
      data->nFileSizeHigh = static_cast<DWORD>(size >> 32);
      data->nFileSizeLow = static_cast<DWORD>(size & 0xFFFFFFFFULL);
    }
    else {
      data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    }
    const std::wstring wideName = meos_compat::utf8ToWide(name.c_str());
    wcsncpy_s(data->cFileName, wideName.c_str(), _TRUNCATE);
    return true;
  }
  return false;
}

struct ErrorMessage {
  DWORD code;
  const wchar_t *text;
};

const ErrorMessage errorMessages[] = {
  {ERROR_SUCCESS, L"The operation completed successfully."},
  {ERROR_FILE_NOT_FOUND, L"The system cannot find the file specified."},
  {ERROR_PATH_NOT_FOUND, L"The system cannot find the path specified."},
  {ERROR_ACCESS_DENIED, L"Access is denied."},
  {ERROR_INVALID_HANDLE, L"The handle is invalid."},
  {ERROR_NOT_ENOUGH_MEMORY, L"Not enough memory resources are available to process this command."},
  {ERROR_NO_MORE_FILES, L"There are no more files."},
  {ERROR_SHARING_VIOLATION, L"The process cannot access the file because it is being used by another process."},
  {ERROR_FILE_EXISTS, L"The file exists."},
  {ERROR_INVALID_PARAMETER, L"The parameter is incorrect."},
  {ERROR_BUFFER_OVERFLOW, L"The file name is too long."},
  {ERROR_DISK_FULL, L"There is not enough space on the disk."},
  {ERROR_INSUFFICIENT_BUFFER, L"The data area passed to a system call is too small."},
  {ERROR_DIR_NOT_EMPTY, L"The directory is not empty."},
  {ERROR_ALREADY_EXISTS, L"Cannot create a file when that file already exists."},
};

} // namespace

DWORD meos_platform::win32ErrorFromErrno(int error) {
  switch (error) {
    case 0:
      return ERROR_SUCCESS;
    case ENOENT:
      return ERROR_FILE_NOT_FOUND;
    case ENOTDIR:
      return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS:
    case EISDIR:
      return ERROR_ACCESS_DENIED;
    case EBADF:
      return ERROR_INVALID_HANDLE;
    case ENOMEM:
      return ERROR_NOT_ENOUGH_MEMORY;
    case EBUSY:
    case ETXTBSY:
      return ERROR_SHARING_VIOLATION;
    case EEXIST:
      return ERROR_ALREADY_EXISTS;
    case EINVAL:
      return ERROR_INVALID_PARAMETER;
    case ENAMETOOLONG:
      return ERROR_BUFFER_OVERFLOW;
    case ENOSPC:
      return ERROR_DISK_FULL;
    default:
      return ERROR_ACCESS_DENIED;
  }
}

DWORD GetLastError() {
  return lastError;
}

void SetLastError(DWORD error) {
  lastError = error;
}

HANDLE CreateFile(LPCWSTR fileName, DWORD access, DWORD /*shareMode*/, LPSECURITY_ATTRIBUTES /*security*/,
                  DWORD disposition, DWORD /*flags*/, HANDLE /*templateFile*/) {
  if (!fileName) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;
  }

  const bool read = (access & GENERIC_READ) != 0;
  const bool write = (access & GENERIC_WRITE) != 0;
  int flags = O_CLOEXEC | (read && write ? O_RDWR : write ? O_WRONLY : O_RDONLY);
  switch (disposition) {
    case CREATE_NEW:
      flags |= O_CREAT | O_EXCL;
      break;
    case CREATE_ALWAYS:
      flags |= O_CREAT | O_TRUNC;
      break;
    case OPEN_EXISTING:
      break;
    case OPEN_ALWAYS:
      flags |= O_CREAT;
      break;
    case TRUNCATE_EXISTING:
      flags |= O_TRUNC;
      break;
    default:
      SetLastError(ERROR_INVALID_PARAMETER);
      return INVALID_HANDLE_VALUE;
  }

  const std::string path = meos_compat::nativePath(fileName);

  // Device names such as \\.\COM3 always denote a serial port.
  if (path.compare(0, 4, "//./") == 0) {
    std::shared_ptr<meos_platform::Win32Object> port =
        meos_platform::openSerialPort(path.substr(4), read, write);
    return port ? meos_platform::registerObject(std::move(port)) : INVALID_HANDLE_VALUE;
  }

  // O_NOCTTY and O_NONBLOCK matter for terminals, and have no effect on regular files.
  const int fd = ::open(path.c_str(), flags | O_NOCTTY | O_NONBLOCK, 0666);
  if (fd < 0) {
    const int error = errno;
    SetLastError(disposition == CREATE_NEW && error == EEXIST ? ERROR_FILE_EXISTS
                                                               : meos_platform::win32ErrorFromErrno(error));
    return INVALID_HANDLE_VALUE;
  }
  SetLastError(ERROR_SUCCESS);

  if (::isatty(fd))
    return meos_platform::registerObject(meos_platform::makeSerialObject(fd));

  ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
  return meos_platform::registerObject(std::make_shared<meos_platform::FileObject>(fd));
}

BOOL DeleteFile(LPCWSTR fileName) {
  if (!fileName || ::unlink(meos_compat::nativePath(fileName).c_str()) != 0) {
    SetLastError(fileName ? meos_platform::win32ErrorFromErrno(errno) : ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return TRUE;
}

BOOL CopyFile(LPCWSTR existingFileName, LPCWSTR newFileName, BOOL failIfExists) {
  if (!existingFileName || !newFileName) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const std::filesystem::path from = meos_compat::nativePath(existingFileName);
  const std::filesystem::path to = meos_compat::nativePath(newFileName);
  std::error_code ec;
  const auto options = failIfExists ? std::filesystem::copy_options::none
                                    : std::filesystem::copy_options::overwrite_existing;
  if (!std::filesystem::copy_file(from, to, options, ec)) {
    SetLastError(ec ? (ec.value() == EEXIST ? ERROR_FILE_EXISTS : meos_platform::win32ErrorFromErrno(ec.value()))
                    : ERROR_FILE_EXISTS);
    return FALSE;
  }
  // Windows keeps the modification time of the source file.
  const auto modified = std::filesystem::last_write_time(from, ec);
  if (!ec)
    std::filesystem::last_write_time(to, modified, ec);
  return TRUE;
}

BOOL CreateDirectory(LPCWSTR pathName, LPSECURITY_ATTRIBUTES /*security*/) {
  if (!pathName) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (::mkdir(meos_compat::nativePath(pathName).c_str(), 0777) != 0) {
    // A missing parent directory is a missing path on Windows.
    SetLastError(errno == ENOENT ? ERROR_PATH_NOT_FOUND : meos_platform::win32ErrorFromErrno(errno));
    return FALSE;
  }
  return TRUE;
}

DWORD GetTempPath(DWORD bufferLength, LPWSTR buffer) {
  const char *environment = std::getenv("TMPDIR");
  std::wstring path = meos_compat::utf8ToWide(environment && *environment ? environment : "/tmp");
  if (path.empty() || path.back() != L'/')
    path.push_back(L'/');
  if (!buffer || bufferLength < path.size() + 1)
    return static_cast<DWORD>(path.size() + 1);
  std::wmemcpy(buffer, path.c_str(), path.size() + 1);
  return static_cast<DWORD>(path.size());
}

UINT GetTempFileName(LPCWSTR pathName, LPCWSTR prefixString, UINT unique, LPWSTR tempFileName) {
  if (!pathName || !tempFileName) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  std::wstring base(pathName);
  if (base.size() > MAX_PATH - 14) {
    SetLastError(ERROR_BUFFER_OVERFLOW);
    return 0;
  }
  if (!base.empty() && base.back() != L'/' && base.back() != L'\\')
    base.push_back(base.find(L'\\') != std::wstring::npos ? L'\\' : L'/');
  base += std::wstring(prefixString ? prefixString : L"").substr(0, 3);

  const auto nameFor = [&base](UINT number) {
    wchar_t hex[16];
    swprintf_s(hex, L"%X.TMP", number & 0xFFFF);
    return base + hex;
  };

  if (unique != 0) {
    const std::wstring name = nameFor(unique);
    std::wmemcpy(tempFileName, name.c_str(), name.size() + 1);
    return unique & 0xFFFF;
  }

  struct stat st;
  if (::stat(meos_compat::nativePath(pathName).c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    SetLastError(ERROR_DIRECTORY);
    return 0;
  }
  const UINT start = (static_cast<UINT>(GetTickCount()) ^ static_cast<UINT>(::getpid())) & 0xFFFF;
  for (UINT k = 0; k < 0x10000; k++) {
    const UINT number = ((start + k) & 0xFFFF) == 0 ? 1 : (start + k) & 0xFFFF;
    const std::wstring name = nameFor(number);
    const int fd = ::open(meos_compat::nativePath(name.c_str()).c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
    if (fd >= 0) {
      ::close(fd);
      std::wmemcpy(tempFileName, name.c_str(), name.size() + 1);
      return number;
    }
    if (errno != EEXIST) {
      SetLastError(meos_platform::win32ErrorFromErrno(errno));
      return 0;
    }
  }
  SetLastError(ERROR_FILE_EXISTS);
  return 0;
}

DWORD GetModuleFileName(HMODULE module, LPWSTR fileName, DWORD size) {
  if (module || !fileName || size == 0) {
    SetLastError(module ? ERROR_MOD_NOT_FOUND : ERROR_INVALID_PARAMETER);
    return 0;
  }
  char exe[PATH_MAX];
  const ssize_t length = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (length < 0) {
    SetLastError(meos_platform::win32ErrorFromErrno(errno));
    return 0;
  }
  exe[length] = 0;
  const std::wstring path = meos_compat::utf8ToWide(exe);
  // As on Windows: a truncated, null-terminated name and ERROR_INSUFFICIENT_BUFFER.
  if (path.size() + 1 > size) {
    std::wmemcpy(fileName, path.c_str(), size - 1);
    fileName[size - 1] = 0;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return size;
  }
  std::wmemcpy(fileName, path.c_str(), path.size() + 1);
  SetLastError(ERROR_SUCCESS);
  return static_cast<DWORD>(path.size());
}

BOOL RemoveDirectory(LPCWSTR pathName) {
  if (!pathName) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (::rmdir(meos_compat::nativePath(pathName).c_str()) != 0) {
    SetLastError(errno == ENOTEMPTY || errno == EEXIST ? ERROR_DIR_NOT_EMPTY
                                                       : meos_platform::win32ErrorFromErrno(errno));
    return FALSE;
  }
  return TRUE;
}

DWORD GetFileSize(HANDLE file, LPDWORD fileSizeHigh) {
  const std::shared_ptr<meos_platform::FileObject> object = meos_platform::fromHandle<meos_platform::FileObject>(file);
  struct stat st;
  if (!object || ::fstat(object->fd, &st) != 0) {
    SetLastError(ERROR_INVALID_HANDLE);
    return INVALID_FILE_SIZE;
  }
  const std::uint64_t size = static_cast<std::uint64_t>(st.st_size);
  if (fileSizeHigh)
    *fileSizeHigh = static_cast<DWORD>(size >> 32);
  // A size whose low part is INVALID_FILE_SIZE is told apart by GetLastError.
  SetLastError(ERROR_SUCCESS);
  return static_cast<DWORD>(size & 0xFFFFFFFFULL);
}

BOOL GetFileTime(HANDLE file, LPFILETIME creationTime, LPFILETIME lastAccessTime, LPFILETIME lastWriteTime) {
  const std::shared_ptr<meos_platform::FileObject> object = meos_platform::fromHandle<meos_platform::FileObject>(file);
  struct stat st;
  if (!object || ::fstat(object->fd, &st) != 0) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (creationTime)
    *creationTime = toFileTime(st.st_ctim);
  if (lastAccessTime)
    *lastAccessTime = toFileTime(st.st_atim);
  if (lastWriteTime)
    *lastWriteTime = toFileTime(st.st_mtim);
  return TRUE;
}

BOOL SetFileTime(HANDLE file, const FILETIME * /*creationTime*/, const FILETIME *lastAccessTime,
                 const FILETIME *lastWriteTime) {
  const std::shared_ptr<meos_platform::FileObject> object = meos_platform::fromHandle<meos_platform::FileObject>(file);
  if (!object) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  timespec times[2];
  for (int k = 0; k < 2; k++) {
    const FILETIME *ft = k == 0 ? lastAccessTime : lastWriteTime;
    if (!ft) {
      times[k].tv_sec = 0;
      times[k].tv_nsec = UTIME_OMIT;
      continue;
    }
    const std::int64_t ticks =
        static_cast<std::int64_t>((static_cast<std::uint64_t>(ft->dwHighDateTime) << 32) | ft->dwLowDateTime) -
        fileTimeUnixEpoch;
    const std::int64_t seconds = ticks >= 0 ? ticks / 10000000LL : -((-ticks + 9999999LL) / 10000000LL);
    times[k].tv_sec = static_cast<time_t>(seconds);
    times[k].tv_nsec = static_cast<long>((ticks - seconds * 10000000LL) * 100);
  }
  if (::futimens(object->fd, times) != 0) {
    SetLastError(meos_platform::win32ErrorFromErrno(errno));
    return FALSE;
  }
  return TRUE;
}

DWORD GetFileAttributes(LPCWSTR fileName) {
  if (!fileName) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_FILE_ATTRIBUTES;
  }
  const std::string path = meos_compat::nativePath(fileName);
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) {
    SetLastError(meos_platform::win32ErrorFromErrno(errno));
    return INVALID_FILE_ATTRIBUTES;
  }
  const std::size_t slash = path.rfind('/');
  return attributesOf(path, slash == std::string::npos ? path : path.substr(slash + 1), st);
}

DWORD GetCurrentDirectory(DWORD bufferLength, LPWSTR buffer) {
  char cwd[PATH_MAX];
  if (!::getcwd(cwd, sizeof(cwd))) {
    SetLastError(meos_platform::win32ErrorFromErrno(errno));
    return 0;
  }
  const std::wstring directory = meos_compat::utf8ToWide(cwd);
  if (!buffer || bufferLength < directory.size() + 1)
    return static_cast<DWORD>(directory.size() + 1);
  std::wmemcpy(buffer, directory.c_str(), directory.size() + 1);
  return static_cast<DWORD>(directory.size());
}

BOOL SetCurrentDirectory(LPCWSTR path) {
  if (!path) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (::chdir(meos_compat::nativePath(path).c_str()) != 0) {
    SetLastError(meos_platform::win32ErrorFromErrno(errno));
    return FALSE;
  }
  return TRUE;
}

HANDLE FindFirstFile(LPCWSTR fileName, LPWIN32_FIND_DATA findData) {
  if (!fileName || !findData) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;
  }

  const std::string path = meos_compat::nativePath(fileName);
  const std::size_t slash = path.rfind('/');
  auto find = std::make_shared<FindHandle>();
  find->directory = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);

  // Windows patterns know only '*' and '?'; '*.*' also matches names without a dot.
  const std::string pattern = slash == std::string::npos ? path : path.substr(slash + 1);
  if (pattern == "*.*") {
    find->pattern = "*";
  }
  else {
    for (char ch : pattern) {
      if (ch == '[' || ch == ']')
        find->pattern += '\\';
      find->pattern += ch;
    }
  }
  if (find->pattern.empty()) {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }

  find->dir = ::opendir(find->directory.c_str());
  if (!find->dir) {
    const int error = errno;
    SetLastError(error == ENOENT || error == ENOTDIR ? ERROR_PATH_NOT_FOUND
                                                    : meos_platform::win32ErrorFromErrno(error));
    return INVALID_HANDLE_VALUE;
  }
  if (!findNextMatch(*find, findData)) {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
  return meos_platform::registerObject(std::move(find));
}

BOOL FindNextFile(HANDLE findFile, LPWIN32_FIND_DATA findData) {
  const std::shared_ptr<FindHandle> find = meos_platform::fromHandle<FindHandle>(findFile);
  if (!find || !findData) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (!findNextMatch(*find, findData)) {
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
  }
  return TRUE;
}

BOOL FindClose(HANDLE findFile) {
  if (!meos_platform::fromHandle<FindHandle>(findFile)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  meos_platform::unregisterObject(findFile);
  return TRUE;
}

DWORD FormatMessage(DWORD flags, LPCVOID /*source*/, DWORD messageId, DWORD /*languageId*/, LPWSTR buffer,
                    DWORD size, va_list * /*arguments*/) {
  std::wstring text;
  for (const ErrorMessage &message : errorMessages) {
    if (message.code == messageId) {
      text = message.text;
      break;
    }
  }
  if (text.empty())
    text = L"Error " + std::to_wstring(messageId) + L".";
  text += L"\r\n";

  if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
    // buffer points to a variable that receives the allocation (freed with LocalFree).
    wchar_t *allocated = static_cast<wchar_t *>(std::malloc((text.size() + 1) * sizeof(wchar_t)));
    if (!allocated) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return 0;
    }
    std::wmemcpy(allocated, text.c_str(), text.size() + 1);
    *reinterpret_cast<LPWSTR *>(buffer) = allocated;
    return static_cast<DWORD>(text.size());
  }
  if (!buffer || size < text.size() + 1) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  std::wmemcpy(buffer, text.c_str(), text.size() + 1);
  return static_cast<DWORD>(text.size());
}

HLOCAL LocalFree(HLOCAL memory) {
  std::free(memory);
  return nullptr;
}

// Windows returns the NetBIOS name: upper case, at most MAX_COMPUTERNAME_LENGTH characters.
BOOL GetComputerName(LPWSTR buffer, LPDWORD size) {
  if (!size) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  char host[256] = {0};
  if (::gethostname(host, sizeof(host) - 1) != 0) {
    SetLastError(meos_platform::win32ErrorFromErrno(errno));
    return FALSE;
  }
  std::string name(host);
  name = name.substr(0, std::min(name.find('.'), static_cast<std::size_t>(MAX_COMPUTERNAME_LENGTH)));
  for (char &ch : name)
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

  const std::wstring wide = meos_compat::utf8ToWide(name.c_str());
  if (!buffer || *size < wide.size() + 1) {
    *size = static_cast<DWORD>(wide.size() + 1);
    SetLastError(ERROR_BUFFER_OVERFLOW);
    return FALSE;
  }
  std::wmemcpy(buffer, wide.c_str(), wide.size() + 1);
  *size = static_cast<DWORD>(wide.size());
  return TRUE;
}

DWORD GetCurrentThreadId() {
  return static_cast<DWORD>(::gettid());
}
