/************************************************************************
    MeOS - Orienteering Software
    Linux port: MSVC compatibility layer.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Provides the Windows integer type names and the MSVC CRT extensions that the
// portable parts of MeOS use, so that they compile unchanged with GCC/Clang.
// Operating system functionality (windows, GDI, serial ports, sockets) is not
// emulated here; that belongs to the platform backends.

#pragma once

#ifndef _WIN32

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// MSVC's standard library headers include each other more widely than libstdc++'s,
// and MeOS relies on that in several places.
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <typeinfo>

#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------------------------------------------------------------
   Integer types with the sizes they have on Windows (LLP64).
   Note: LONG is 32 bit on Windows, but long is 64 bit on Linux.
   --------------------------------------------------------------------- */
typedef std::uint8_t   BYTE;
typedef std::uint16_t  WORD;
typedef std::uint32_t  DWORD;
typedef std::int16_t   SHORT;
typedef std::int32_t   LONG;
typedef std::uint32_t  ULONG;
typedef unsigned int   UINT;
typedef int            BOOL;
typedef std::int32_t   HRESULT;
typedef std::uintptr_t UINT_PTR;
typedef std::intptr_t  INT_PTR;
typedef std::uintptr_t DWORD_PTR;
typedef std::intptr_t  LONG_PTR;

typedef wchar_t        WCHAR;
typedef char          *LPSTR;
typedef const char    *LPCSTR;
typedef wchar_t       *LPWSTR;
typedef const wchar_t *LPCWSTR;
typedef void          *LPVOID;
typedef const void    *LPCVOID;
typedef DWORD         *LPDWORD;

typedef int errno_t;

// A typedef rather than a macro: the code uses functional casts such as __int64(x).
// std::int64_t (long on Linux) rather than long long keeps __int64 and int64_t the
// same type, as with MSVC; the code binds references across both names.
typedef std::int64_t __int64;

// Microsoft-specific keywords used by vendored third-party headers (e.g. libharu).
#define __declspec(x)
#define __stdcall
#define __cdecl

// Source annotations (SAL). They document how a parameter is used and have no effect
// on the generated code; MeOS writes them on the dialog procedures in meos.cpp.
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_

// MSVC's architecture macro. MeOS uses it to add overloads that are only distinct from
// the size_t ones on 64-bit targets (gdistructures.h).
#if defined(__x86_64__) && !defined(_M_X64)
#define _M_X64 100
#endif

// MSVC declares type_info in the global namespace as well.
using std::type_info;

#define _TRUNCATE ((std::size_t)-1)
#define STRUNCATE 80

#define _MAX_PATH  260
#define _MAX_DRIVE 3
#define _MAX_DIR   256
#define _MAX_FNAME 256
#define _MAX_EXT   256

#define _countof(a) (sizeof(a) / sizeof((a)[0]))

namespace meos_compat {

/* ---------------------------------------------------------------------
   UTF-32 (wchar_t on Linux) <-> UTF-8.
   --------------------------------------------------------------------- */
inline std::string wideToUtf8(const wchar_t *w, std::size_t len) {
  std::string out;
  out.reserve(len + len / 2);
  for (std::size_t i = 0; i < len; i++) {
    std::uint32_t c = static_cast<std::uint32_t>(w[i]);
    if ((c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF)
      c = 0xFFFD;
    if (c < 0x80) {
      out += static_cast<char>(c);
    }
    else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

inline std::string wideToUtf8(const wchar_t *w) {
  return w ? wideToUtf8(w, std::wcslen(w)) : std::string();
}

// Invalid or truncated sequences decode to U+FFFD, one per offending byte.
inline std::wstring utf8ToWide(const char *s, std::size_t len) {
  static const std::uint32_t minValue[] = {0, 0x80, 0x800, 0x10000};
  std::wstring out;
  out.reserve(len);
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
  std::size_t i = 0;
  while (i < len) {
    const unsigned char lead = p[i];
    std::uint32_t c;
    std::size_t trail;
    if (lead < 0x80) { c = lead; trail = 0; }
    else if ((lead & 0xE0) == 0xC0) { c = lead & 0x1F; trail = 1; }
    else if ((lead & 0xF0) == 0xE0) { c = lead & 0x0F; trail = 2; }
    else if ((lead & 0xF8) == 0xF0) { c = lead & 0x07; trail = 3; }
    else { out += wchar_t(0xFFFD); i++; continue; }

    bool valid = i + trail < len;
    for (std::size_t k = 1; valid && k <= trail; k++) {
      if ((p[i + k] & 0xC0) != 0x80)
        valid = false;
      else
        c = (c << 6) | (p[i + k] & 0x3F);
    }
    if (!valid || c < minValue[trail] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
      out += wchar_t(0xFFFD);
      i++;
      continue;
    }
    out += static_cast<wchar_t>(c);
    i += trail + 1;
  }
  return out;
}

inline std::wstring utf8ToWide(const char *s) {
  return s ? utf8ToWide(s, std::strlen(s)) : std::wstring();
}

// MeOS builds paths with '\' as separator. File names are passed to the operating
// system as UTF-8 with '/' as separator.
inline std::string nativePath(const wchar_t *path) {
  std::string native = wideToUtf8(path);
  std::replace(native.begin(), native.end(), '\\', '/');
  return native;
}

/* ---------------------------------------------------------------------
   Wide printf format strings follow MSVC semantics in MeOS: %s and %c
   denote wchar_t arguments, %S and %C (or %hs, %hc) narrow ones. The ISO C
   functions used on Linux interpret %s as char*, so formats are translated
   before use. GCC does not check wide format strings, which is why a
   missing translation would fail silently at runtime.
   --------------------------------------------------------------------- */
inline std::wstring translateWideFormat(const wchar_t *fmt) {
  std::wstring out;
  out.reserve(std::wcslen(fmt) + 8);
  const wchar_t *p = fmt;
  auto isDigitOrStar = [](wchar_t ch) { return ch == L'*' || (ch >= L'0' && ch <= L'9'); };

  while (*p) {
    if (*p != L'%') {
      out += *p++;
      continue;
    }
    out += *p++;
    if (*p == L'%') {
      out += *p++;
      continue;
    }
    while (*p && std::wcschr(L"-+ #0", *p))
      out += *p++;
    while (isDigitOrStar(*p))
      out += *p++;
    if (*p == L'.') {
      out += *p++;
      while (isDigitOrStar(*p))
        out += *p++;
    }

    enum class CharWidth { Default, Narrow, Wide } charWidth = CharWidth::Default;
    std::wstring length;
    if (p[0] == L'I' && p[1] == L'6' && p[2] == L'4') {
      length = L"ll";
      p += 3;
    }
    else if (p[0] == L'I' && p[1] == L'3' && p[2] == L'2') {
      p += 3;
    }
    else if (p[0] == L'I') {
      length = L"z";
      p += 1;
    }
    else if (p[0] == L'h' && (p[1] == L's' || p[1] == L'c')) {
      charWidth = CharWidth::Narrow;
      p += 1;
    }
    else if ((p[0] == L'l' || p[0] == L'w') && (p[1] == L's' || p[1] == L'c')) {
      charWidth = CharWidth::Wide;
      p += 1;
    }
    else {
      while (*p && std::wcschr(L"hlLqjzt", *p))
        length += *p++;
    }

    wchar_t conv = *p;
    if (!conv)
      break;
    p++;

    switch (conv) {
      case L's':
        out += charWidth == CharWidth::Narrow ? L"s" : L"ls";
        break;
      case L'c':
        out += charWidth == CharWidth::Narrow ? L"c" : L"lc";
        break;
      case L'S':
        out += charWidth == CharWidth::Wide ? L"ls" : L"s";
        break;
      case L'C':
        out += charWidth == CharWidth::Wide ? L"lc" : L"c";
        break;
      default:
        out += length;
        out += conv;
        break;
    }
  }
  return out;
}

inline int vswprintfMsvc(wchar_t *buf, std::size_t size, const wchar_t *fmt, va_list args) {
  if (!buf || size == 0)
    return -1;
  if (!fmt) {
    buf[0] = 0;
    return -1;
  }
  const std::wstring translated = translateWideFormat(fmt);
  int written = std::vswprintf(buf, size, translated.c_str(), args);
  if (written < 0)
    buf[0] = 0; // Overflow: the MSVC secure variants leave an empty string as well.
  return written;
}

inline int vsprintfMsvc(char *buf, std::size_t size, const char *fmt, va_list args) {
  if (!buf || size == 0)
    return -1;
  if (!fmt) {
    buf[0] = 0;
    return -1;
  }
  int written = std::vsnprintf(buf, size, fmt, args);
  if (written < 0 || static_cast<std::size_t>(written) >= size) {
    buf[0] = 0;
    return -1;
  }
  return written;
}

// Copies at most count characters. A string that does not fit is cut off if truncate is
// set (the _TRUNCATE behaviour); otherwise dst becomes empty and ERANGE is returned.
template<typename Char>
inline errno_t copyString(Char *dst, std::size_t size, const Char *src, std::size_t count, bool truncate) {
  if (!dst || size == 0)
    return EINVAL;
  if (!src) {
    dst[0] = 0;
    return EINVAL;
  }
  std::size_t len = 0;
  while (len < count && src[len])
    len++;
  if (len >= size) {
    if (truncate) {
      std::memcpy(dst, src, (size - 1) * sizeof(Char));
      dst[size - 1] = 0;
      return STRUNCATE;
    }
    dst[0] = 0;
    return ERANGE;
  }
  std::memcpy(dst, src, len * sizeof(Char));
  dst[len] = 0;
  return 0;
}

template<typename Char>
inline errno_t appendString(Char *dst, std::size_t size, const Char *src) {
  if (!dst || size == 0)
    return EINVAL;
  std::size_t used = 0;
  while (used < size && dst[used])
    used++;
  if (used == size) {
    dst[0] = 0;
    return EINVAL;
  }
  if (copyString(dst + used, size - used, src, _TRUNCATE, false) != 0) {
    dst[0] = 0;
    return ERANGE;
  }
  return 0;
}

template<typename Char>
inline errno_t unsignedToString(unsigned long long magnitude, bool negative, Char *buf, std::size_t size, int radix) {
  if (!buf || size == 0)
    return EINVAL;
  if (radix < 2 || radix > 36) {
    buf[0] = 0;
    return EINVAL;
  }
  Char tmp[72];
  std::size_t n = 0;
  do {
    const int digit = static_cast<int>(magnitude % radix);
    tmp[n++] = static_cast<Char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
    magnitude /= radix;
  } while (magnitude);
  if (negative)
    tmp[n++] = static_cast<Char>('-');
  if (n + 1 > size) {
    buf[0] = 0;
    return ERANGE;
  }
  for (std::size_t k = 0; k < n; k++)
    buf[k] = tmp[n - 1 - k];
  buf[n] = 0;
  return 0;
}

// As in MSVC, only radix 10 produces a sign; other radixes print the two's
// complement bit pattern of a 32 bit (_itoa) or 64 bit (_i64toa) value.
template<typename Char>
inline errno_t integerToString(long long value, Char *buf, std::size_t size, int radix, bool is64Bit) {
  const bool negative = radix == 10 && value < 0;
  unsigned long long magnitude;
  if (negative)
    magnitude = 0ULL - static_cast<unsigned long long>(value);
  else if (radix == 10 || is64Bit)
    magnitude = static_cast<unsigned long long>(value);
  else
    magnitude = static_cast<std::uint32_t>(value);
  return unsignedToString(magnitude, negative, buf, size, radix);
}

} // namespace meos_compat

/* ---------------------------------------------------------------------
   Formatted input and output (sized and array-template variants).
   --------------------------------------------------------------------- */
inline int swprintf_s(wchar_t *buf, std::size_t size, const wchar_t *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = meos_compat::vswprintfMsvc(buf, size, fmt, args);
  va_end(args);
  return written;
}

template<std::size_t N>
inline int swprintf_s(wchar_t (&buf)[N], const wchar_t *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = meos_compat::vswprintfMsvc(buf, N, fmt, args);
  va_end(args);
  return written;
}

inline int vswprintf_s(wchar_t *buf, std::size_t size, const wchar_t *fmt, va_list args) {
  return meos_compat::vswprintfMsvc(buf, size, fmt, args);
}

inline int sprintf_s(char *buf, std::size_t size, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = meos_compat::vsprintfMsvc(buf, size, fmt, args);
  va_end(args);
  return written;
}

template<std::size_t N>
inline int sprintf_s(char (&buf)[N], const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = meos_compat::vsprintfMsvc(buf, N, fmt, args);
  va_end(args);
  return written;
}

inline int wprintf_s(const wchar_t *fmt, ...) {
  if (!fmt)
    return -1;
  const std::wstring translated = meos_compat::translateWideFormat(fmt);
  va_list args;
  va_start(args, fmt);
  int written = std::vwprintf(translated.c_str(), args);
  va_end(args);
  return written;
}

// Reads at most as many elements as fit into the buffer.
inline std::size_t fread_s(void *buffer, std::size_t bufferSize, std::size_t elementSize, std::size_t count,
                           FILE *stream) {
  if (!buffer || !stream || elementSize == 0)
    return 0;
  return std::fread(buffer, elementSize, std::min(count, bufferSize / elementSize), stream);
}

/* ---------------------------------------------------------------------
   Secure CRT string functions.
   --------------------------------------------------------------------- */
inline errno_t wcscpy_s(wchar_t *dst, std::size_t size, const wchar_t *src) {
  return meos_compat::copyString(dst, size, src, _TRUNCATE, false);
}
template<std::size_t N>
inline errno_t wcscpy_s(wchar_t (&dst)[N], const wchar_t *src) { return wcscpy_s(dst, N, src); }

inline errno_t strcpy_s(char *dst, std::size_t size, const char *src) {
  return meos_compat::copyString(dst, size, src, _TRUNCATE, false);
}
template<std::size_t N>
inline errno_t strcpy_s(char (&dst)[N], const char *src) { return strcpy_s(dst, N, src); }

inline errno_t wcsncpy_s(wchar_t *dst, std::size_t size, const wchar_t *src, std::size_t count) {
  return meos_compat::copyString(dst, size, src, count, count == _TRUNCATE);
}
template<std::size_t N>
inline errno_t wcsncpy_s(wchar_t (&dst)[N], const wchar_t *src, std::size_t count) {
  return wcsncpy_s(dst, N, src, count);
}

inline errno_t strncpy_s(char *dst, std::size_t size, const char *src, std::size_t count) {
  return meos_compat::copyString(dst, size, src, count, count == _TRUNCATE);
}
template<std::size_t N>
inline errno_t strncpy_s(char (&dst)[N], const char *src, std::size_t count) {
  return strncpy_s(dst, N, src, count);
}

inline errno_t wcscat_s(wchar_t *dst, std::size_t size, const wchar_t *src) {
  return meos_compat::appendString(dst, size, src);
}
template<std::size_t N>
inline errno_t wcscat_s(wchar_t (&dst)[N], const wchar_t *src) { return wcscat_s(dst, N, src); }

inline errno_t strcat_s(char *dst, std::size_t size, const char *src) {
  return meos_compat::appendString(dst, size, src);
}
template<std::size_t N>
inline errno_t strcat_s(char (&dst)[N], const char *src) { return strcat_s(dst, N, src); }

/* ---------------------------------------------------------------------
   Integer to string.
   --------------------------------------------------------------------- */
inline errno_t _itow_s(int value, wchar_t *buf, std::size_t size, int radix) {
  return meos_compat::integerToString<wchar_t>(value, buf, size, radix, false);
}
template<std::size_t N>
inline errno_t _itow_s(int value, wchar_t (&buf)[N], int radix) { return _itow_s(value, buf, N, radix); }

inline errno_t _itoa_s(int value, char *buf, std::size_t size, int radix) {
  return meos_compat::integerToString<char>(value, buf, size, radix, false);
}
template<std::size_t N>
inline errno_t _itoa_s(int value, char (&buf)[N], int radix) { return _itoa_s(value, buf, N, radix); }

inline errno_t _i64toa_s(long long value, char *buf, std::size_t size, int radix) {
  return meos_compat::integerToString<char>(value, buf, size, radix, true);
}

inline errno_t _i64tow_s(long long value, wchar_t *buf, std::size_t size, int radix) {
  return meos_compat::integerToString<wchar_t>(value, buf, size, radix, true);
}

// unsigned long is 32 bit on Windows; the value is truncated accordingly.
inline errno_t _ultow_s(unsigned long value, wchar_t *buf, std::size_t size, int radix) {
  return meos_compat::unsignedToString<wchar_t>(static_cast<std::uint32_t>(value), false, buf, size, radix);
}
template<std::size_t N>
inline errno_t _ultow_s(unsigned long value, wchar_t (&buf)[N], int radix) { return _ultow_s(value, buf, N, radix); }

inline errno_t _ultoa_s(unsigned long value, char *buf, std::size_t size, int radix) {
  return meos_compat::unsignedToString<char>(static_cast<std::uint32_t>(value), false, buf, size, radix);
}
template<std::size_t N>
inline errno_t _ultoa_s(unsigned long value, char (&buf)[N], int radix) { return _ultoa_s(value, buf, N, radix); }

inline errno_t _ui64tow_s(unsigned long long value, wchar_t *buf, std::size_t size, int radix) {
  return meos_compat::unsignedToString<wchar_t>(value, false, buf, size, radix);
}

inline errno_t _ui64toa_s(unsigned long long value, char *buf, std::size_t size, int radix) {
  return meos_compat::unsignedToString<char>(value, false, buf, size, radix);
}

/* ---------------------------------------------------------------------
   Conversions and comparisons.
   --------------------------------------------------------------------- */
inline int _wtoi(const wchar_t *s) { return static_cast<int>(std::wcstol(s, nullptr, 10)); }
inline long long _wtoi64(const wchar_t *s) { return std::wcstoll(s, nullptr, 10); }
inline double _wtof(const wchar_t *s) { return std::wcstod(s, nullptr); }
inline long long _atoi64(const char *s) { return std::strtoll(s, nullptr, 10); }

inline long long _wcstoi64(const wchar_t *s, wchar_t **end, int base) { return std::wcstoll(s, end, base); }
inline unsigned long long _wcstoui64(const wchar_t *s, wchar_t **end, int base) { return std::wcstoull(s, end, base); }
inline long long _strtoi64(const char *s, char **end, int base) { return std::strtoll(s, end, base); }
inline unsigned long long _strtoui64(const char *s, char **end, int base) { return std::strtoull(s, end, base); }

inline int _stricmp(const char *a, const char *b) { return ::strcasecmp(a, b); }
inline int _strcmpi(const char *a, const char *b) { return ::strcasecmp(a, b); }
inline int _strnicmp(const char *a, const char *b, std::size_t n) { return ::strncasecmp(a, b, n); }
inline int _wcsicmp(const wchar_t *a, const wchar_t *b) { return ::wcscasecmp(a, b); }
inline int _wcsnicmp(const wchar_t *a, const wchar_t *b, std::size_t n) { return ::wcsncasecmp(a, b, n); }

inline int _memicmp(const void *a, const void *b, std::size_t n) {
  const unsigned char *pa = static_cast<const unsigned char *>(a);
  const unsigned char *pb = static_cast<const unsigned char *>(b);
  for (std::size_t i = 0; i < n; i++) {
    const int ca = (pa[i] >= 'A' && pa[i] <= 'Z') ? pa[i] + 32 : pa[i];
    const int cb = (pb[i] >= 'A' && pb[i] <= 'Z') ? pb[i] + 32 : pb[i];
    if (ca != cb)
      return ca - cb;
  }
  return 0;
}

inline int iswascii(wint_t ch) { return ch < 0x80; }

/* ---------------------------------------------------------------------
   Path functions. Both '/' and '\' are accepted as separators.
   --------------------------------------------------------------------- */
inline errno_t _wsplitpath_s(const wchar_t *path,
                             wchar_t *drive, std::size_t driveSize, wchar_t *dir, std::size_t dirSize,
                             wchar_t *fname, std::size_t fnameSize, wchar_t *ext, std::size_t extSize) {
  auto clearAll = [&]() {
    if (drive && driveSize) drive[0] = 0;
    if (dir && dirSize) dir[0] = 0;
    if (fname && fnameSize) fname[0] = 0;
    if (ext && extSize) ext[0] = 0;
  };
  if (!path) {
    clearAll();
    return EINVAL;
  }

  const std::size_t length = std::wcslen(path);
  const std::size_t driveEnd = (length >= 2 && path[1] == L':') ? 2 : 0;
  std::size_t dirEnd = driveEnd;
  for (std::size_t i = driveEnd; i < length; i++) {
    if (path[i] == L'/' || path[i] == L'\\')
      dirEnd = i + 1;
  }
  std::size_t extStart = length;
  for (std::size_t i = length; i > dirEnd; i--) {
    if (path[i - 1] == L'.') {
      extStart = i - 1;
      break;
    }
  }

  auto put = [](wchar_t *out, std::size_t outSize, const wchar_t *from, std::size_t count) {
    if (!out)
      return true;
    if (count + 1 > outSize)
      return false;
    std::wmemcpy(out, from, count);
    out[count] = 0;
    return true;
  };
  if (!put(drive, driveSize, path, driveEnd) ||
      !put(dir, dirSize, path + driveEnd, dirEnd - driveEnd) ||
      !put(fname, fnameSize, path + dirEnd, extStart - dirEnd) ||
      !put(ext, extSize, path + extStart, length - extStart)) {
    clearAll();
    return ERANGE;
  }
  return 0;
}

template<std::size_t ND, std::size_t NP, std::size_t NF, std::size_t NE>
inline errno_t _wsplitpath_s(const wchar_t *path, wchar_t (&drive)[ND], wchar_t (&dir)[NP],
                             wchar_t (&fname)[NF], wchar_t (&ext)[NE]) {
  return _wsplitpath_s(path, drive, ND, dir, NP, fname, NF, ext, NE);
}

// Resolves a path to an absolute, normalized path without requiring it to exist.
inline wchar_t *_wfullpath(wchar_t *absPath, const wchar_t *relPath, std::size_t maxLength) {
  if (!relPath)
    return nullptr;
  std::wstring path(relPath);
  std::replace(path.begin(), path.end(), L'\\', L'/');
  if (path.empty() || path[0] != L'/') {
    char cwd[PATH_MAX];
    if (!::getcwd(cwd, sizeof(cwd)))
      return nullptr;
    path = meos_compat::utf8ToWide(cwd) + L"/" + path;
  }

  std::vector<std::wstring> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find(L'/', start);
    if (end == std::wstring::npos)
      end = path.size();
    std::wstring part = path.substr(start, end - start);
    if (part == L"..") {
      if (!parts.empty())
        parts.pop_back();
    }
    else if (!part.empty() && part != L".") {
      parts.push_back(part);
    }
    start = end + 1;
  }
  std::wstring result;
  for (const std::wstring &part : parts)
    result += L"/" + part;
  if (result.empty())
    result = L"/";

  if (!absPath) {
    maxLength = result.size() + 1;
    absPath = static_cast<wchar_t *>(std::malloc(maxLength * sizeof(wchar_t)));
    if (!absPath)
      return nullptr;
  }
  if (result.size() + 1 > maxLength)
    return nullptr;
  std::wmemcpy(absPath, result.c_str(), result.size() + 1);
  return absPath;
}

/* ---------------------------------------------------------------------
   File name based CRT functions.
   --------------------------------------------------------------------- */
#define _stat stat

inline int _wremove(const wchar_t *file) {
  return std::remove(meos_compat::nativePath(file).c_str());
}

inline int _wrename(const wchar_t *from, const wchar_t *to) {
  return std::rename(meos_compat::nativePath(from).c_str(), meos_compat::nativePath(to).c_str());
}

// The access mode values (0 = exists, 2 = write, 4 = read) match F_OK, W_OK and R_OK.
inline int _waccess(const wchar_t *file, int mode) {
  return ::access(meos_compat::nativePath(file).c_str(), mode);
}

inline int _wstat(const wchar_t *file, struct stat *st) {
  return ::stat(meos_compat::nativePath(file).c_str(), st);
}

typedef std::int64_t __time64_t;

// Inverse of gmtime: the tm fields are UTC (onlineinput.cpp).
inline __time64_t _mkgmtime64(std::tm *utc) {
  return static_cast<__time64_t>(::timegm(utc));
}

inline int _wmkdir(const wchar_t *dir) {
  return ::mkdir(meos_compat::nativePath(dir).c_str(), 0777);
}

inline errno_t _wfopen_s(FILE **fp, const wchar_t *file, const wchar_t *mode) {
  if (!fp)
    return EINVAL;
  std::string narrowMode = meos_compat::wideToUtf8(mode);
  std::size_t ccs = narrowMode.find(",ccs=");
  if (ccs != std::string::npos)
    narrowMode.erase(ccs);
  *fp = std::fopen(meos_compat::nativePath(file).c_str(), narrowMode.c_str());
  return *fp ? 0 : errno;
}

inline FILE *_wfopen(const wchar_t *file, const wchar_t *mode) {
  FILE *fp = nullptr;
  _wfopen_s(&fp, file, mode);
  return fp;
}

// The minizip headers shipped with MeOS map fopen64 to _wfopen for MSVC, and zip.cpp
// calls it with wide names. This overload keeps glibc's fopen64(const char *, ...).
inline FILE *fopen64(const wchar_t *file, const wchar_t *mode) {
  return _wfopen(file, mode);
}

#endif // !_WIN32
