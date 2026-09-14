/************************************************************************
    MeOS - Orienteering Software
    Linux port: Win32 date and time functions.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// SYSTEMTIME and FILETIME functions with the Windows semantics MeOS relies on,
// including the validation in SystemTimeToFileTime that rejects invalid dates.

#include "windows.h"

#include <cstdint>
#include <ctime>
#include <locale.h>
#include <string>
#include <time.h>

namespace {

// 100-nanosecond intervals between 1601-01-01 (FILETIME epoch) and 1970-01-01.
constexpr std::int64_t fileTimeUnixEpoch = 116444736000000000LL;
constexpr std::int64_t ticksPerMillisecond = 10000LL;
constexpr std::int64_t millisecondsPerDay = 86400000LL;

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
  std::int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

// Days since 1970-01-01 in the proleptic Gregorian calendar (algorithm by Howard Hinnant).
std::int64_t daysFromCivil(std::int64_t year, unsigned month, unsigned day) {
  year -= month <= 2;
  const std::int64_t era = floorDiv(year, 400);
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<std::int64_t>(dayOfEra) - 719468;
}

void civilFromDays(std::int64_t days, std::int64_t &year, unsigned &month, unsigned &day) {
  days += 719468;
  const std::int64_t era = floorDiv(days, 146097);
  const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
  const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthIndex = (5 * dayOfYear + 2) / 153;
  day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
  month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
  year = static_cast<std::int64_t>(yearOfEra) + era * 400 + (month <= 2);
}

unsigned daysInMonth(unsigned year, unsigned month) {
  static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29 : days[month - 1];
}

void fillSystemTime(const std::tm &tm, long nanoseconds, LPSYSTEMTIME st) {
  st->wYear = static_cast<WORD>(tm.tm_year + 1900);
  st->wMonth = static_cast<WORD>(tm.tm_mon + 1);
  st->wDayOfWeek = static_cast<WORD>(tm.tm_wday);
  st->wDay = static_cast<WORD>(tm.tm_mday);
  st->wHour = static_cast<WORD>(tm.tm_hour);
  st->wMinute = static_cast<WORD>(tm.tm_min);
  st->wSecond = static_cast<WORD>(tm.tm_sec > 59 ? 59 : tm.tm_sec); // no leap seconds on Windows
  st->wMilliseconds = static_cast<WORD>(nanoseconds / 1000000);
}

} // namespace

void GetLocalTime(LPSYSTEMTIME st) {
  timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  std::tm local;
  localtime_r(&now.tv_sec, &local);
  fillSystemTime(local, now.tv_nsec, st);
}

void GetSystemTime(LPSYSTEMTIME st) {
  timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  std::tm utc;
  gmtime_r(&now.tv_sec, &utc);
  fillSystemTime(utc, now.tv_nsec, st);
}

BOOL SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft) {
  if (!st || !ft)
    return FALSE;
  // Same validation as Windows; wDayOfWeek is ignored.
  if (st->wYear < 1601 || st->wYear > 30827 || st->wMonth < 1 || st->wMonth > 12 || st->wDay < 1 ||
      st->wDay > daysInMonth(st->wYear, st->wMonth) || st->wHour > 23 || st->wMinute > 59 ||
      st->wSecond > 59 || st->wMilliseconds > 999)
    return FALSE;

  const std::int64_t milliseconds =
      daysFromCivil(st->wYear, st->wMonth, st->wDay) * millisecondsPerDay +
      ((st->wHour * 60LL + st->wMinute) * 60LL + st->wSecond) * 1000LL + st->wMilliseconds;
  const std::uint64_t ticks = static_cast<std::uint64_t>(milliseconds * ticksPerMillisecond + fileTimeUnixEpoch);
  ft->dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
  ft->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
  return TRUE;
}

BOOL FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st) {
  if (!ft || !st)
    return FALSE;
  const std::uint64_t raw = (static_cast<std::uint64_t>(ft->dwHighDateTime) << 32) | ft->dwLowDateTime;
  if (raw >= 0x8000000000000000ULL)
    return FALSE;

  const std::int64_t milliseconds = floorDiv(static_cast<std::int64_t>(raw) - fileTimeUnixEpoch, ticksPerMillisecond);
  const std::int64_t days = floorDiv(milliseconds, millisecondsPerDay);
  std::int64_t millisecondOfDay = milliseconds - days * millisecondsPerDay;

  std::int64_t year;
  unsigned month, day;
  civilFromDays(days, year, month, day);

  st->wYear = static_cast<WORD>(year);
  st->wMonth = static_cast<WORD>(month);
  st->wDay = static_cast<WORD>(day);
  st->wDayOfWeek = static_cast<WORD>(((days % 7) + 7 + 4) % 7); // 1970-01-01 was a Thursday
  st->wMilliseconds = static_cast<WORD>(millisecondOfDay % 1000);
  millisecondOfDay /= 1000;
  st->wSecond = static_cast<WORD>(millisecondOfDay % 60);
  millisecondOfDay /= 60;
  st->wMinute = static_cast<WORD>(millisecondOfDay % 60);
  st->wHour = static_cast<WORD>(millisecondOfDay / 60);
  return TRUE;
}

// Only the current time zone (timeZone == nullptr) is supported.
BOOL SystemTimeToTzSpecificLocalTime(const TIME_ZONE_INFORMATION *timeZone, const SYSTEMTIME *universalTime,
                                     LPSYSTEMTIME localTime) {
  if (timeZone || !universalTime || !localTime)
    return FALSE;
  FILETIME ft;
  if (!SystemTimeToFileTime(universalTime, &ft))
    return FALSE;
  const std::int64_t ticks =
      static_cast<std::int64_t>((static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
  const std::int64_t unixMilliseconds = floorDiv(ticks - fileTimeUnixEpoch, ticksPerMillisecond);
  const time_t seconds = static_cast<time_t>(floorDiv(unixMilliseconds, 1000));
  std::tm local;
  if (!localtime_r(&seconds, &local))
    return FALSE;
  fillSystemTime(local, static_cast<long>(unixMilliseconds - static_cast<std::int64_t>(seconds) * 1000) * 1000000L,
                 localTime);
  return TRUE;
}

// Milliseconds since system start, including suspended time as on Windows.
unsigned long long GetTickCount64() {
  timespec now;
  clock_gettime(CLOCK_BOOTTIME, &now);
  return static_cast<unsigned long long>(now.tv_sec) * 1000ULL + static_cast<unsigned long long>(now.tv_nsec / 1000000);
}

DWORD GetTickCount() {
  return static_cast<DWORD>(GetTickCount64());
}

namespace {

// Formats a date or time with the user locale's default representation and returns
// it in the ANSI code page, as the Windows "A" functions do.
int formatLocalized(const SYSTEMTIME *st, const char *conversion, LPSTR buffer, int size) {
  std::tm tm{};
  if (st) {
    tm.tm_year = st->wYear - 1900;
    tm.tm_mon = st->wMonth - 1;
    tm.tm_mday = st->wDay;
    tm.tm_wday = st->wDayOfWeek;
    tm.tm_hour = st->wHour;
    tm.tm_min = st->wMinute;
    tm.tm_sec = st->wSecond;
  }
  else {
    const time_t now = time(nullptr);
    localtime_r(&now, &tm);
  }

  static const locale_t userLocale = [] {
    locale_t locale = newlocale(LC_ALL_MASK, "", static_cast<locale_t>(0));
    return locale ? locale : newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(0));
  }();

  char utf8[256];
  const std::size_t length = strftime_l(utf8, sizeof(utf8), conversion, &tm, userLocale);
  if (length == 0)
    return 0;

  const std::wstring wide = meos_compat::utf8ToWide(utf8, length);
  const int needed = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (size == 0)
    return needed;
  if (!buffer || size < needed)
    return 0;
  return WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, buffer, size, nullptr, nullptr);
}

} // namespace

// Only the locale's default format (format == nullptr) is supported.
int GetDateFormatA(LCID /*locale*/, DWORD /*flags*/, const SYSTEMTIME *date, LPCSTR format, LPSTR buffer, int size) {
  return format ? 0 : formatLocalized(date, "%x", buffer, size);
}

int GetTimeFormatA(LCID /*locale*/, DWORD /*flags*/, const SYSTEMTIME *time, LPCSTR format, LPSTR buffer, int size) {
  return format ? 0 : formatLocalized(time, "%X", buffer, size);
}

// Only the current time zone (timeZone == nullptr) is supported.
BOOL TzSpecificLocalTimeToSystemTime(const TIME_ZONE_INFORMATION *timeZone, const SYSTEMTIME *localTime,
                                     LPSYSTEMTIME universalTime) {
  if (timeZone || !localTime || !universalTime)
    return FALSE;
  FILETIME check;
  if (!SystemTimeToFileTime(localTime, &check))
    return FALSE;

  std::tm local{};
  local.tm_year = localTime->wYear - 1900;
  local.tm_mon = localTime->wMonth - 1;
  local.tm_mday = localTime->wDay;
  local.tm_hour = localTime->wHour;
  local.tm_min = localTime->wMinute;
  local.tm_sec = localTime->wSecond;
  local.tm_isdst = -1;
  const time_t seconds = mktime(&local);
  std::tm utc;
  if (seconds == static_cast<time_t>(-1) || !gmtime_r(&seconds, &utc))
    return FALSE;
  fillSystemTime(utc, static_cast<long>(localTime->wMilliseconds) * 1000000L, universalTime);
  return TRUE;
}

namespace {

std::int64_t currentUtcOffsetTicks() {
  const time_t now = time(nullptr);
  std::tm local;
  if (!localtime_r(&now, &local))
    return 0;
  return static_cast<std::int64_t>(local.tm_gmtoff) * 10000000LL;
}

std::int64_t ticksOf(const FILETIME &ft) {
  return static_cast<std::int64_t>((static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
}

void setTicks(LPFILETIME ft, std::int64_t ticks) {
  ft->dwLowDateTime = static_cast<DWORD>(static_cast<std::uint64_t>(ticks) & 0xFFFFFFFFULL);
  ft->dwHighDateTime = static_cast<DWORD>(static_cast<std::uint64_t>(ticks) >> 32);
}

} // namespace

// As on Windows, the current UTC offset is applied regardless of the date.
BOOL LocalFileTimeToFileTime(const FILETIME *localFileTime, LPFILETIME fileTime) {
  if (!localFileTime || !fileTime)
    return FALSE;
  setTicks(fileTime, ticksOf(*localFileTime) - currentUtcOffsetTicks());
  return TRUE;
}

// MS-DOS date: day (bits 0-4), month (5-8), years since 1980 (9-15);
// time: seconds / 2 (bits 0-4), minute (5-10), hour (11-15).
BOOL DosDateTimeToFileTime(WORD fatDate, WORD fatTime, LPFILETIME fileTime) {
  if (!fileTime)
    return FALSE;
  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(1980 + (fatDate >> 9));
  st.wMonth = static_cast<WORD>((fatDate >> 5) & 0x0F);
  st.wDay = static_cast<WORD>(fatDate & 0x1F);
  st.wHour = static_cast<WORD>(fatTime >> 11);
  st.wMinute = static_cast<WORD>((fatTime >> 5) & 0x3F);
  st.wSecond = static_cast<WORD>((fatTime & 0x1F) * 2);
  return SystemTimeToFileTime(&st, fileTime);
}

// Only the years 1980 to 2107 can be represented.
BOOL FileTimeToDosDateTime(const FILETIME *fileTime, LPWORD fatDate, LPWORD fatTime) {
  SYSTEMTIME st;
  if (!fileTime || !fatDate || !fatTime || !FileTimeToSystemTime(fileTime, &st) || st.wYear < 1980 ||
      st.wYear > 2107)
    return FALSE;
  *fatDate = static_cast<WORD>(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
  *fatTime = static_cast<WORD>((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
  return TRUE;
}

// As on Windows, the current UTC offset is applied regardless of the date.
BOOL FileTimeToLocalFileTime(const FILETIME *fileTime, LPFILETIME localFileTime) {
  if (!fileTime || !localFileTime)
    return FALSE;
  setTicks(localFileTime, ticksOf(*fileTime) + currentUtcOffsetTicks());
  return TRUE;
}
