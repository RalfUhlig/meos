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

#include <cstdio>
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

} // namespace

int main() {
  testWideFormat();
  testUtf8();
  testCodePages();
  testTime();
  testPathsAndIntegers();
  testFiles();

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("platform self test passed\n");
  return 0;
}
