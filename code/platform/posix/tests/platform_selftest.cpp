/************************************************************************
    MeOS - Orienteering Software
    Linux port: self test of the platform compatibility layer.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Checks the Win32 and MSVC CRT emulation in code/platform against the Windows
// behaviour MeOS relies on. Exits with a non-zero status if a check fails.

#include "StdAfx.h"

#include <iphlpapi.h>
#include <process.h>
#include <wininet.h>
#include <winsock2.h>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace {

int failures = 0;

#define CHECK(expr)                                                                   \
  do {                                                                                \
    if (!(expr)) {                                                                    \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr);   \
      failures++;                                                                     \
    }                                                                                 \
  } while (false)

void testWideFormat() {
  CHECK(meos_compat::translateWideFormat(L"%s|%-8s|%c|%S|%hs|%ls|%%|%d|%I64d|%.2f") ==
        L"%ls|%-8ls|%lc|%s|%s|%ls|%%|%d|%lld|%.2f");

  wchar_t buffer[64];
  swprintf_s(buffer, L"%s-%d-%c", L"V\u00e4lj", 42, L'x');
  CHECK(std::wstring(buffer) == L"V\u00e4lj-42-x");
  CHECK(swprintf_s(buffer, 4, L"%s", L"too long") < 0 && buffer[0] == 0);

  char narrow[16];
  CHECK(sprintf_s(narrow, "%02d:%02d", 7, 5) == 5 && std::string(narrow) == "07:05");
}

void testUtf8() {
  const std::wstring text = L"V\u00e4lj \u0416 \U0001F600";
  const std::string utf8 = meos_compat::wideToUtf8(text.c_str());
  CHECK(utf8 == "V\xC3\xA4lj \xD0\x96 \xF0\x9F\x98\x80");
  CHECK(meos_compat::utf8ToWide(utf8.c_str()) == text);
  CHECK(meos_compat::utf8ToWide("a\xFF" "b") == L"a\uFFFDb");
  CHECK(meos_compat::utf8ToWide("\xE2\x82") == L"\uFFFD\uFFFD");
}

void testCodePages() {
  // Narrow string literals use the ANSI code page, as with MSVC.
  CHECK(std::string("\u00e4") == "\xE4");

  const char cp1252[] = "\xE4\x80\x81";
  wchar_t wide[8] = {0};
  CHECK(MultiByteToWideChar(1252, 0, cp1252, 3, wide, 8) == 3);
  CHECK(wide[0] == L'\u00e4' && wide[1] == L'\u20ac' && wide[2] == L'\u0081');
  CHECK(MultiByteToWideChar(CP_ACP, 0, cp1252, -1, nullptr, 0) == 4);
  CHECK(MultiByteToWideChar(1251, 0, "\xC6", 1, wide, 8) == 1 && wide[0] == L'\u0416');

  char narrow[8] = {0};
  BOOL usedDefault = FALSE;
  CHECK(WideCharToMultiByte(1252, 0, L"\u00e4\u0416", 2, narrow, 8, "?", &usedDefault) == 2);
  CHECK(narrow[0] == '\xE4' && narrow[1] == '?' && usedDefault == TRUE);
  CHECK(WideCharToMultiByte(CP_UTF8, 0, L"x", 1, narrow, 8, "?", nullptr) == 0);

  CHECK(CompareString(LOCALE_USER_DEFAULT, NORM_IGNORECASE, L"abc", -1, L"ABC", -1) == CSTR_EQUAL);
  CHECK(CompareString(LOCALE_USER_DEFAULT, 0, L"abc", -1, L"abd", -1) == CSTR_LESS_THAN);
  CHECK(lstrcmpi(L"Meos", L"MEOS") == 0);

  wchar_t lower[] = L"\u00c5SA \u00d6stberg";
  CHECK(CharLower(lower) == lower && std::wstring(lower) == L"\u00e5sa \u00f6stberg");
  CHECK((std::uintptr_t)CharLower((LPWSTR)(std::uintptr_t)L'\u00c4') == L'\u00e4');
}

void testTime() {
  SYSTEMTIME st = {};
  st.wYear = 2026;
  st.wMonth = 9;
  st.wDay = 11;
  st.wHour = 20;
  st.wMinute = 17;
  st.wSecond = 5;
  st.wMilliseconds = 123;
  FILETIME ft;
  CHECK(SystemTimeToFileTime(&st, &ft));
  SYSTEMTIME back = {};
  CHECK(FileTimeToSystemTime(&ft, &back));
  CHECK(back.wYear == 2026 && back.wMonth == 9 && back.wDay == 11 && back.wHour == 20 &&
        back.wMinute == 17 && back.wSecond == 5 && back.wMilliseconds == 123);
  CHECK(back.wDayOfWeek == 5); // Friday

  SYSTEMTIME epoch = {};
  epoch.wYear = 1970;
  epoch.wMonth = 1;
  epoch.wDay = 1;
  CHECK(SystemTimeToFileTime(&epoch, &ft));
  CHECK(((static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) == 116444736000000000ULL);

  SYSTEMTIME first = {};
  first.wYear = 1601;
  first.wMonth = 1;
  first.wDay = 1;
  CHECK(SystemTimeToFileTime(&first, &ft) && ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0);

  // Invalid dates are rejected, as by Windows.
  SYSTEMTIME date = st;
  date.wMonth = 2;
  date.wDay = 30;
  CHECK(!SystemTimeToFileTime(&date, &ft));
  date.wDay = 29;
  date.wYear = 2024;
  CHECK(SystemTimeToFileTime(&date, &ft));
  date.wYear = 2100;
  CHECK(!SystemTimeToFileTime(&date, &ft));

  SYSTEMTIME now = {};
  GetLocalTime(&now);
  CHECK(now.wYear >= 2026 && now.wMonth >= 1 && now.wMonth <= 12);
  CHECK(GetTickCount64() > 0);
}

void testDosTimes() {
  // 2026-09-14 12:34:56 as MS-DOS date and time (two-second resolution).
  const WORD fatDate = WORD((46 << 9) | (9 << 5) | 14);
  const WORD fatTime = WORD((12 << 11) | (34 << 5) | 28);
  FILETIME ft;
  CHECK(DosDateTimeToFileTime(fatDate, fatTime, &ft));
  SYSTEMTIME st;
  CHECK(FileTimeToSystemTime(&ft, &st));
  CHECK(st.wYear == 2026 && st.wMonth == 9 && st.wDay == 14 && st.wHour == 12 && st.wMinute == 34 && st.wSecond == 56);
  WORD date = 0, time = 0;
  CHECK(FileTimeToDosDateTime(&ft, &date, &time) && date == fatDate && time == fatTime);
  SYSTEMTIME early = {1979, 12, 0, 31, 0, 0, 0, 0};
  CHECK(SystemTimeToFileTime(&early, &ft) && !FileTimeToDosDateTime(&ft, &date, &time));

  // LocalFileTimeToFileTime undoes FileTimeToLocalFileTime.
  SYSTEMTIME noon = {2026, 1, 0, 15, 12, 0, 0, 0};
  FILETIME utc, local, back;
  CHECK(SystemTimeToFileTime(&noon, &utc));
  CHECK(FileTimeToLocalFileTime(&utc, &local) && LocalFileTimeToFileTime(&local, &back));
  CHECK(back.dwLowDateTime == utc.dwLowDateTime && back.dwHighDateTime == utc.dwHighDateTime);

  // _mkgmtime64 reads the fields as UTC (onlineinput.cpp).
  std::tm tm = {};
  tm.tm_year = 70;
  tm.tm_mon = 0;
  tm.tm_mday = 2;
  tm.tm_hour = 1;
  tm.tm_isdst = -1;
  CHECK(_mkgmtime64(&tm) == 86400 + 3600);
}

void testPathsAndIntegers() {
  wchar_t drive[_MAX_DRIVE], dir[_MAX_DIR], fname[_MAX_FNAME], ext[_MAX_EXT];
  CHECK(_wsplitpath_s(L"C:\\dir\\sub\\file.name.xml", drive, dir, fname, ext) == 0);
  CHECK(std::wstring(drive) == L"C:" && std::wstring(dir) == L"\\dir\\sub\\" &&
        std::wstring(fname) == L"file.name" && std::wstring(ext) == L".xml");
  CHECK(_wsplitpath_s(L"/home/meos/list", drive, dir, fname, ext) == 0);
  CHECK(std::wstring(dir) == L"/home/meos/" && std::wstring(fname) == L"list" && std::wstring(ext).empty());

  wchar_t wbuf[40];
  char buf[40];
  CHECK(_itow_s(-5, wbuf, 10) == 0 && std::wstring(wbuf) == L"-5");
  CHECK(_itoa_s(-1, buf, 16) == 0 && std::string(buf) == "ffffffff");
  CHECK(_i64toa_s(-1, buf, sizeof(buf), 16) == 0 && std::string(buf) == "ffffffffffffffff");
  CHECK(_ui64tow_s(18446744073709551615ULL, wbuf, 40, 10) == 0 && std::wstring(wbuf) == L"18446744073709551615");

  wchar_t small[4];
  CHECK(wcsncpy_s(small, L"abcdef", _TRUNCATE) == STRUNCATE && std::wstring(small) == L"abc");
  CHECK(wcscpy_s(small, L"abcdef") != 0 && small[0] == 0);

  CHECK(sizeof(LONG) == 4 && sizeof(DWORD) == 4 && sizeof(WORD) == 2 && sizeof(__int64) == 8);
  CHECK(sizeof(RECT) == 16 && sizeof(SYSTEMTIME) == 16 && sizeof(FILETIME) == 8);
}

void testFiles() {
  const std::filesystem::path base =
      std::filesystem::temp_directory_path() / ("meos-selftest-" + std::to_string(::getpid()));
  std::filesystem::create_directories(base);
  const std::wstring dir = base.wstring();

  for (const wchar_t *name : {L"a.xml", L"B.XML", L"c.txt"}) {
    HANDLE file = CreateFile((dir + L"\\" + name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE)
      CHECK(CloseHandle(file));
  }
  CHECK(CreateFile((dir + L"/a.xml").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr) ==
        INVALID_HANDLE_VALUE);
  CHECK(GetLastError() == ERROR_FILE_EXISTS);

  WIN32_FIND_DATA fd;
  int matches = 0;
  HANDLE find = FindFirstFile((dir + L"\\*.xml").c_str(), &fd);
  CHECK(find != INVALID_HANDLE_VALUE);
  if (find != INVALID_HANDLE_VALUE) {
    do {
      matches++;
    } while (FindNextFile(find, &fd));
    CHECK(GetLastError() == ERROR_NO_MORE_FILES);
    CHECK(FindClose(find));
  }
  CHECK(matches == 2); // matching is case-insensitive

  CHECK((GetFileAttributes(dir.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0);
  CHECK(GetFileAttributes((dir + L"\\missing").c_str()) == INVALID_FILE_ATTRIBUTES);
  CHECK(!CopyFile((dir + L"\\a.xml").c_str(), (dir + L"\\c.txt").c_str(), TRUE) &&
        GetLastError() == ERROR_FILE_EXISTS);
  CHECK(CopyFile((dir + L"\\a.xml").c_str(), (dir + L"\\d.xml").c_str(), TRUE));
  CHECK(DeleteFile((dir + L"\\d.xml").c_str()));
  CHECK(!DeleteFile((dir + L"\\d.xml").c_str()) && GetLastError() == ERROR_FILE_NOT_FOUND);

  wchar_t previous[MAX_PATH];
  CHECK(GetCurrentDirectory(MAX_PATH, previous) > 0);
  CHECK(SetCurrentDirectory((dir + L"\\").c_str()));
  CHECK(std::filesystem::equivalent(std::filesystem::current_path(), base));
  CHECK(!SetCurrentDirectory((dir + L"\\missing").c_str()) && GetLastError() == ERROR_FILE_NOT_FOUND);
  CHECK(SetCurrentDirectory(previous));

  std::ofstream out(meosPath(dir + L"\\stream.txt"));
  out << "ok";
  out.close();
  CHECK(std::filesystem::exists(base / "stream.txt"));

  // _wfopen and the wide fopen64 of the minizip headers (zip.cpp).
  FILE *wide = _wfopen((dir + L"\\stream.txt").c_str(), L"rb");
  CHECK(wide != nullptr);
  if (wide) {
    char text[4] = {0};
    CHECK(std::fread(text, 1, 3, wide) == 2 && std::string(text) == "ok");
    std::fclose(wide);
  }
  wide = fopen64((dir + L"\\missing.txt").c_str(), L"rb");
  CHECK(wide == nullptr);

  // Size and times of an open file (download.cpp, zip.cpp).
  HANDLE sized = CreateFile((dir + L"\\stream.txt").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(sized != INVALID_HANDLE_VALUE);
  DWORD high = 1;
  CHECK(GetFileSize(sized, &high) == 2 && high == 0);
  FILETIME created, accessed, written;
  CHECK(GetFileTime(sized, &created, &accessed, &written));
  SYSTEMTIME st = {2024, 5, 0, 17, 10, 30, 42, 0};
  FILETIME stamp;
  CHECK(SystemTimeToFileTime(&st, &stamp));
  CHECK(SetFileTime(sized, nullptr, nullptr, &stamp));
  FILETIME after, accessedAfter;
  CHECK(GetFileTime(sized, nullptr, &accessedAfter, &after));
  CHECK(after.dwLowDateTime == stamp.dwLowDateTime && after.dwHighDateTime == stamp.dwHighDateTime);
  CHECK(accessedAfter.dwLowDateTime == accessed.dwLowDateTime && accessedAfter.dwHighDateTime == accessed.dwHighDateTime);
  CHECK(CloseHandle(sized));
  CHECK(GetFileSize(sized, nullptr) == INVALID_FILE_SIZE && GetLastError() == ERROR_INVALID_HANDLE);

  // Directories.
  CHECK(CreateDirectory((dir + L"\\sub").c_str(), nullptr));
  CHECK(!CreateDirectory((dir + L"\\sub").c_str(), nullptr) && GetLastError() == ERROR_ALREADY_EXISTS);
  CHECK(!CreateDirectory((dir + L"\\missing\\sub").c_str(), nullptr) && GetLastError() == ERROR_PATH_NOT_FOUND);
  CHECK(CopyFile((dir + L"\\a.xml").c_str(), (dir + L"\\sub\\a.xml").c_str(), TRUE));
  CHECK(!RemoveDirectory((dir + L"\\sub").c_str()) && GetLastError() == ERROR_DIR_NOT_EMPTY);
  CHECK(DeleteFile((dir + L"\\sub\\a.xml").c_str()));
  CHECK(RemoveDirectory((dir + L"\\sub").c_str()));
  CHECK(!RemoveDirectory((dir + L"\\sub").c_str()) && GetLastError() == ERROR_FILE_NOT_FOUND);

  // Temporary files (meos.cpp: getTempPath, getTempFile).
  wchar_t tempPath[MAX_PATH];
  const DWORD tempLength = GetTempPath(MAX_PATH, tempPath);
  CHECK(tempLength > 0 && tempLength == std::wcslen(tempPath) && tempPath[tempLength - 1] == L'/');
  CHECK(GetTempPath(1, nullptr) == tempLength + 1);
  wchar_t tempName[MAX_PATH];
  const UINT number = GetTempFileName(dir.c_str(), L"meosx", 0, tempName);
  CHECK(number != 0);
  const std::wstring created1(tempName);
  CHECK(created1.find(dir + L"/meo") == 0 && created1.size() > 8 && created1.substr(created1.size() - 4) == L".TMP");
  CHECK(GetFileAttributes(tempName) != INVALID_FILE_ATTRIBUTES); // created, empty
  CHECK(GetTempFileName(dir.c_str(), L"ix", 0, tempName) != 0 && created1 != tempName);
  CHECK(GetTempFileName((dir + L"\\ix").c_str(), L"ix", 0, tempName) == 0 && GetLastError() == ERROR_DIRECTORY);
  CHECK(GetTempFileName(dir.c_str(), L"ab", 0x1234, tempName) == 0x1234);
  CHECK(std::wstring(tempName) == dir + L"/ab1234.TMP" && GetFileAttributes(tempName) == INVALID_FILE_ATTRIBUTES);

  // The program's own file (meos.cpp: exePath).
  wchar_t module[MAX_PATH];
  const DWORD moduleLength = GetModuleFileName(nullptr, module, MAX_PATH);
  CHECK(moduleLength > 0 && std::wstring(module).find(L"meos_platform_selftest") != std::wstring::npos);
  wchar_t shortModule[8];
  CHECK(GetModuleFileName(nullptr, shortModule, 8) == 8 && shortModule[7] == 0 &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER);

  std::filesystem::remove_all(base);
}

void testThreads() {
  CRITICAL_SECTION section;
  InitializeCriticalSection(&section);

  // Windows critical sections are recursive.
  EnterCriticalSection(&section);
  EnterCriticalSection(&section);
  LeaveCriticalSection(&section);

  int counter = 0;
  std::thread other([&] {
    EnterCriticalSection(&section);
    counter = 1;
    LeaveCriticalSection(&section);
  });
  Sleep(50);
  CHECK(counter == 0); // The section is still held here.
  LeaveCriticalSection(&section);
  other.join();
  CHECK(counter == 1);
  DeleteCriticalSection(&section);

  const std::uint64_t start = GetTickCount64();
  Sleep(120);
  CHECK(GetTickCount64() - start >= 100);

  static std::atomic<bool> ran{false};
  const HANDLE thread = reinterpret_cast<HANDLE>(_beginthread([](void *) { ran = true; }, 0, nullptr));
  DWORD exitCode = 0;
  for (int wait = 0; wait < 100 && !ran; wait++)
    Sleep(10);
  CHECK(ran);
  // The handle of a finished thread is no longer valid, as with _beginthread.
  for (int wait = 0; wait < 100 && GetExitCodeThread(thread, &exitCode); wait++)
    Sleep(10);
  CHECK(!GetExitCodeThread(thread, &exitCode));

  // _beginthreadex (mysqldaemon.cpp): the handle stays valid until CloseHandle and
  // reports the return value of the thread function.
  static std::atomic<bool> release{false};
  static unsigned result = 42;
  const HANDLE threadEx = reinterpret_cast<HANDLE>(_beginthreadex(
      nullptr, 0, [](void *argument) -> unsigned {
        while (!release)
          Sleep(1);
        return *static_cast<unsigned *>(argument);
      }, &result, 0, nullptr));
  CHECK(threadEx != nullptr);
  CHECK(GetExitCodeThread(threadEx, &exitCode) && exitCode == STILL_ACTIVE);
  release = true;
  for (int wait = 0; wait < 100 && GetExitCodeThread(threadEx, &exitCode) && exitCode == STILL_ACTIVE; wait++)
    Sleep(10);
  CHECK(GetExitCodeThread(threadEx, &exitCode) && exitCode == 42);
  CHECK(CloseHandle(threadEx));
  CHECK(!GetExitCodeThread(threadEx, &exitCode));
  CHECK(!CloseHandle(threadEx));
}

// A pseudo terminal stands in for the serial port of an SI master station.
void testSerialPort() {
  const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
  CHECK(master >= 0);
  if (master < 0)
    return;
  CHECK(::grantpt(master) == 0 && ::unlockpt(master) == 0);
  char name[64] = {0};
  CHECK(::ptsname_r(master, name, sizeof(name)) == 0);

  const HANDLE port = CreateFile(meos_compat::utf8ToWide(name).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(port != INVALID_HANDLE_VALUE);
  if (port == INVALID_HANDLE_VALUE) {
    ::close(master);
    return;
  }

  DCB state;
  std::memset(&state, 0, sizeof(state));
  state.DCBlength = sizeof(state);
  state.BaudRate = CBR_38400;
  state.fBinary = TRUE;
  state.fDtrControl = DTR_CONTROL_DISABLE;
  state.fRtsControl = RTS_CONTROL_DISABLE;
  state.Parity = NOPARITY;
  state.StopBits = ONESTOPBIT;
  state.ByteSize = 8;
  CHECK(SetCommState(port, &state));
  DCB reread;
  CHECK(GetCommState(port, &reread));
  CHECK(reread.BaudRate == CBR_38400 && reread.ByteSize == 8 && reread.DCBlength == sizeof(DCB));
  state.BaudRate = 12345; // Not a standard rate.
  CHECK(!SetCommState(port, &state));
  state.BaudRate = CBR_4800;
  CHECK(SetCommState(port, &state));

  COMMTIMEOUTS timeouts = {};
  CHECK(GetCommTimeouts(port, &timeouts));
  timeouts.ReadIntervalTimeout = 50;
  timeouts.ReadTotalTimeoutMultiplier = 10;
  timeouts.ReadTotalTimeoutConstant = 300;
  timeouts.WriteTotalTimeoutMultiplier = 10;
  timeouts.WriteTotalTimeoutConstant = 300;
  CHECK(SetCommTimeouts(port, &timeouts));
  COMMTIMEOUTS restored = {};
  CHECK(GetCommTimeouts(port, &restored) && restored.ReadTotalTimeoutConstant == 300);

  // STX, command, ETX as the SI protocol frames it.
  const BYTE request[] = {0x02, 0xF0, 0x01, 0x03};
  DWORD written = 0;
  CHECK(WriteFile(port, request, sizeof(request), &written, nullptr) && written == sizeof(request));
  BYTE echo[8] = {0};
  CHECK(::read(master, echo, sizeof(request)) == static_cast<ssize_t>(sizeof(request)));
  CHECK(std::memcmp(echo, request, sizeof(request)) == 0);

  const BYTE answer[] = {0x02, 0x02, 0x03};
  CHECK(::write(master, answer, sizeof(answer)) == static_cast<ssize_t>(sizeof(answer)));
  BYTE buffer[8] = {0};
  DWORD got = 0;
  CHECK(ReadFile(port, buffer, sizeof(answer), &got, nullptr) && got == sizeof(answer));
  CHECK(std::memcmp(buffer, answer, sizeof(answer)) == 0);

  // A timeout is not an error: the call succeeds with the bytes read so far.
  const std::uint64_t start = GetTickCount64();
  CHECK(ReadFile(port, buffer, 4, &got, nullptr) && got == 0);
  const std::uint64_t waited = GetTickCount64() - start;
  CHECK(waited >= 300 && waited < 3000); // 10 ms per byte plus 300 ms

  DWORD event = 0;
  CHECK(SetCommMask(port, EV_RXCHAR));
  std::thread sender([master] {
    Sleep(100);
    const BYTE punch = 0xD3;
    const ssize_t sent = ::write(master, &punch, 1);
    (void)sent;
  });
  CHECK(WaitCommEvent(port, &event, nullptr) && event == EV_RXCHAR);
  sender.join();
  CHECK(ReadFile(port, buffer, 1, &got, nullptr) && got == 1 && buffer[0] == 0xD3);

  // TerminateThread ends a thread that waits for the next punch.
  struct Waiter {
    HANDLE port;
    std::atomic<bool> waiting{false};
    std::atomic<bool> done{false};
    std::atomic<DWORD> error{0};
  } waiter{port};
  const HANDLE monitor = reinterpret_cast<HANDLE>(_beginthread(
      [](void *argument) {
        Waiter &state = *static_cast<Waiter *>(argument);
        DWORD mask = 0;
        state.waiting = true;
        if (!WaitCommEvent(state.port, &mask, nullptr))
          state.error = GetLastError();
        state.done = true;
      },
      0, &waiter));
  for (int wait = 0; wait < 100 && !waiter.waiting; wait++)
    Sleep(10);
  Sleep(50);
  CHECK(!waiter.done);
  CHECK(TerminateThread(monitor, 0));
  for (int wait = 0; wait < 100 && !waiter.done; wait++)
    Sleep(10);
  CHECK(waiter.done && waiter.error == ERROR_OPERATION_ABORTED);

  CHECK(CloseHandle(port));
  CHECK(!CloseHandle(port)); // Closing twice is an error, not a crash.
  ::close(master);

  // Port names outside the COM scheme, and ports without a device, fail to open.
  CHECK(CreateFile(L"//./COM999", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) ==
        INVALID_HANDLE_VALUE);
  CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
  CHECK(CreateFile(L"//./LPT1", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) ==
        INVALID_HANDLE_VALUE);

  // QueryDosDevice lists the serial ports as a double null terminated string list.
  wchar_t devices[4096] = {0};
  const DWORD length = QueryDosDevice(nullptr, devices, 4096);
  if (length > 0) {
    CHECK(devices[length - 1] == 0 && devices[length - 2] == 0);
    for (DWORD i = 0; i < length - 1; i += std::wcslen(&devices[i]) + 1)
      CHECK(std::wcsncmp(&devices[i], L"COM", 3) == 0);
  }
  CHECK(QueryDosDevice(nullptr, devices, 1) == 0); // Too small, if there are ports at all.
}

// The calls MonitorTCPSI in SportIdent.cpp makes, over the loopback interface.
void testSockets() {
  WSADATA data;
  CHECK(WSAStartup(0x101, &data) == 0);

  const SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  CHECK(server != INVALID_SOCKET);
  sockaddr_in local = {};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  local.sin_port = 0;
  CHECK(bind(server, (sockaddr *)&local, sizeof(local)) == 0);
  CHECK(listen(server, 1) == 0);
  socklen_t boundLength = sizeof(local);
  CHECK(getsockname(static_cast<int>(server), (sockaddr *)&local, &boundLength) == 0);

  std::thread sender([local] {
    const int client = ::socket(AF_INET, SOCK_STREAM, 0);
    if (::connect(client, (sockaddr *)&local, sizeof(local)) == 0) {
      const ssize_t sent = ::send(client, "punch", 5, 0);
      (void)sent;
    }
    ::close(client);
  });

  sockaddr_in from;
  int fromLength = sizeof(from);
  const SOCKET client = accept(server, (sockaddr *)&from, &fromLength);
  CHECK(client != INVALID_SOCKET);
  CHECK(fromLength == sizeof(sockaddr_in) && from.sin_family == AF_INET);
  char buffer[8] = {0};
  CHECK(recv(client, buffer, 5, MSG_WAITALL) == 5 && std::string(buffer) == "punch");
  sender.join();
  CHECK(closesocket(client) == 0);

  // As in SportIdent::closeCom: shutting the listening socket down ends a blocked accept.
  std::atomic<int> acceptError{0};
  std::thread listener([&] {
    int length = sizeof(from);
    if (accept(server, (sockaddr *)&from, &length) == INVALID_SOCKET)
      acceptError = WSAGetLastError();
  });
  Sleep(50);
  shutdown(server, SD_BOTH);
  listener.join();
  CHECK(acceptError != 0);
  CHECK(closesocket(server) == 0);

  // UDP with the Windows address types, as DirectSocket in socket.cpp.
  CHECK(sizeof(SOCKADDR_IN) == sizeof(sockaddr_in));
  CHECK(offsetof(SOCKADDR_IN, sin_addr) == offsetof(sockaddr_in, sin_addr));
  CHECK(offsetof(SOCKADDR_IN, sin_port) == offsetof(sockaddr_in, sin_port));
  SOCKADDR_IN address;
  std::memset(&address, 0, sizeof(address));
  address.sin_addr.S_un.S_un_b.s_b1 = 127;
  address.sin_addr.S_un.S_un_b.s_b4 = 1;
  CHECK(address.sin_addr.s_addr == htonl(INADDR_LOOPBACK));

  const SOCKET receiver = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  address.sin_family = AF_INET;
  address.sin_port = 0;
  CHECK(bind(receiver, (SOCKADDR *)&address, sizeof(SOCKADDR_IN)) == 0);
  socklen_t addressLength = sizeof(address);
  CHECK(getsockname(static_cast<int>(receiver), (sockaddr *)&address, &addressLength) == 0);

  fd_set fds;
  timeval timeout = {0, 50000};
  FD_ZERO(&fds);
  FD_SET(receiver, &fds);
  CHECK(select(0, &fds, NULL, NULL, &timeout) == 0);
  CHECK(timeout.tv_sec == 0 && timeout.tv_usec == 50000); // Winsock leaves the timeout unchanged

  const SOCKET senderSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  const int message = 4711;
  CHECK(sendto(senderSocket, (char *)&message, sizeof(message), 0, (sockaddr *)&address, sizeof(address)) ==
        sizeof(message));
  FD_ZERO(&fds);
  FD_SET(receiver, &fds);
  timeout = {1, 0};
  CHECK(select(0, &fds, NULL, NULL, &timeout) == 1 && FD_ISSET(receiver, &fds));
  int received = 0;
  SOCKADDR_IN peer;
  int peerLength = sizeof(peer);
  CHECK(recvfrom(receiver, (char *)&received, sizeof(received), 0, (sockaddr *)&peer, &peerLength) ==
        sizeof(received));
  CHECK(received == message && peerLength == sizeof(sockaddr_in) && peer.sin_addr.S_un.S_un_b.s_b1 == 127);
  CHECK(closesocket(senderSocket) == 0 && closesocket(receiver) == 0);
  CHECK(WSACleanup() == 0);
}

// GetAdaptersAddresses as ListIpAddresses in download.cpp calls it.
void testAdapters() {
  ULONG size = 0;
  CHECK(GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &size) == ERROR_BUFFER_OVERFLOW && size > 0);
  std::vector<char> buffer(size);
  auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
  ULONG tooSmall = sizeof(IP_ADAPTER_ADDRESSES);
  CHECK(GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &tooSmall) == ERROR_BUFFER_OVERFLOW &&
        tooSmall == size);
  CHECK(GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, adapters, &size) ==
        ERROR_SUCCESS);

  bool loopback = false;
  for (IP_ADAPTER_ADDRESSES *adapter = adapters; adapter; adapter = adapter->Next) {
    CHECK(adapter->AdapterName != nullptr && adapter->IfIndex > 0);
    for (IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
      CHECK(unicast->Address.lpSockaddr->sa_family == AF_INET); // AF_INET only
      const auto *ipv4 = reinterpret_cast<SOCKADDR_IN *>(unicast->Address.lpSockaddr);
      if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK && ipv4->sin_addr.S_un.S_un_b.s_b1 == 127)
        loopback = true;
    }
  }
  CHECK(loopback);
  CHECK(GetAdaptersAddresses(AF_APPLETALK, 0, nullptr, adapters, &size) == ERROR_INVALID_PARAMETER);
}

// WinInet as download.cpp uses it; until stage 2 no server can be reached.
void testInternet() {
  URL_COMPONENTS uc;
  std::memset(&uc, 0, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  wchar_t host[64], path[64], extra[64];
  uc.lpszHostName = host;
  uc.dwHostNameLength = 64;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 64;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 64;
  const std::wstring url = L"https://user:secret@results.example.org:8443/meos/post%20it.php?id=1%202#top";
  CHECK(InternetCrackUrl(url.c_str(), DWORD(url.size()), ICU_ESCAPE, &uc));
  CHECK(uc.nScheme == INTERNET_SCHEME_HTTPS && uc.nPort == 8443);
  CHECK(std::wstring(host) == L"results.example.org" && uc.dwHostNameLength == 19);
  CHECK(std::wstring(path) == L"/meos/post it.php" && uc.dwUrlPathLength == 17);
  CHECK(std::wstring(extra) == L"?id=1 2#top");

  // Components without buffer point into the URL.
  URL_COMPONENTS parts;
  std::memset(&parts, 0, sizeof(parts));
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = 1;
  parts.dwHostNameLength = 1;
  parts.dwUserNameLength = 1;
  parts.dwPasswordLength = 1;
  parts.dwUrlPathLength = 1;
  CHECK(InternetCrackUrl(url.c_str(), 0, 0, &parts));
  CHECK(std::wstring(parts.lpszScheme, parts.dwSchemeLength) == L"https");
  CHECK(std::wstring(parts.lpszUserName, parts.dwUserNameLength) == L"user");
  CHECK(std::wstring(parts.lpszPassword, parts.dwPasswordLength) == L"secret");
  CHECK(std::wstring(parts.lpszHostName, parts.dwHostNameLength) == L"results.example.org");
  CHECK(std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) == L"/meos/post%20it.php");
  CHECK(parts.lpszUrlPath >= url.c_str() && parts.lpszUrlPath < url.c_str() + url.size());

  std::memset(&parts, 0, sizeof(parts));
  parts.dwStructSize = sizeof(parts);
  CHECK(InternetCrackUrl(L"http://localhost", 0, 0, &parts) && parts.nScheme == INTERNET_SCHEME_HTTP &&
        parts.nPort == INTERNET_DEFAULT_HTTP_PORT);
  wchar_t small[4];
  uc.lpszHostName = small;
  uc.dwHostNameLength = 4;
  CHECK(!InternetCrackUrl(url.c_str(), 0, 0, &uc) && GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
        uc.dwHostNameLength == 20);
  CHECK(!InternetCrackUrl(L"meos.example.org/list", 0, 0, &parts) && GetLastError() == ERROR_INTERNET_UNRECOGNIZED_SCHEME);
  CHECK(!InternetCrackUrl(L"http://host:99999/", 0, 0, &parts) && GetLastError() == ERROR_INTERNET_INVALID_URL);

  const HINTERNET session = InternetOpen(L"MeOS", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
  CHECK(session != nullptr);
  DWORD timeoutMs = 600000;
  CHECK(InternetSetOption(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs)));
  CHECK(InternetOpenUrl(session, L"http://localhost/x", nullptr, 0, INTERNET_FLAG_DONT_CACHE, 0) == nullptr &&
        GetLastError() == ERROR_INTERNET_CANNOT_CONNECT);
  DWORD responseError = 1, responseLength = 16;
  wchar_t response[16] = L"x";
  CHECK(InternetGetLastResponseInfo(&responseError, response, &responseLength) && response[0] == 0 &&
        responseLength == 0);

  const HINTERNET connection = InternetConnect(session, L"localhost", 80, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
  CHECK(connection != nullptr);
  CHECK(HttpOpenRequest(session, L"POST", L"/", HTTP_VERSION, nullptr, nullptr, 0, 0) == nullptr &&
        GetLastError() == ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
  const HINTERNET request = HttpOpenRequest(connection, L"POST", L"/", HTTP_VERSION, nullptr, nullptr, 0, 0);
  CHECK(request != nullptr);
  INTERNET_BUFFERS buffers;
  std::memset(&buffers, 0, sizeof(buffers));
  buffers.dwStructSize = sizeof(buffers);
  CHECK(!HttpSendRequestEx(request, &buffers, nullptr, 0, 0) && GetLastError() == ERROR_INTERNET_CANNOT_CONNECT);
  DWORD status = 0, statusLength = sizeof(status);
  CHECK(!HttpQueryInfo(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusLength, nullptr));
  DWORD read = 7;
  char data[8];
  CHECK(!InternetReadFile(request, data, sizeof(data), &read) && read == 0);

  CHECK(InternetCloseHandle(request) && InternetCloseHandle(connection) && InternetCloseHandle(session));
  CHECK(!InternetCloseHandle(session) && GetLastError() == ERROR_INVALID_HANDLE);
  // Other handles are no internet handles.
  CHECK(!InternetCloseHandle(nullptr));
}

} // namespace

int main() {
  testWideFormat();
  testUtf8();
  testCodePages();
  testTime();
  testDosTimes();
  testPathsAndIntegers();
  testFiles();
  testThreads();
  testSerialPort();
  testSockets();
  testAdapters();
  testInternet();

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("platform self test passed\n");
  return 0;
}
