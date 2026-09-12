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

#include <process.h>
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

  std::ofstream out(meosPath(dir + L"\\stream.txt"));
  out << "ok";
  out.close();
  CHECK(std::filesystem::exists(base / "stream.txt"));

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
  CHECK(WSACleanup() == 0);
}

} // namespace

int main() {
  testWideFormat();
  testUtf8();
  testCodePages();
  testTime();
  testPathsAndIntegers();
  testFiles();
  testThreads();
  testSerialPort();
  testSockets();

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("platform self test passed\n");
  return 0;
}
