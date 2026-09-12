/************************************************************************
    MeOS - Orienteering Software
    Linux port: Win32 string conversion and comparison functions.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// MultiByteToWideChar, WideCharToMultiByte and CompareString with the Windows
// semantics MeOS relies on. Supported code pages are UTF-8 and single byte code
// pages (MeOS uses 1250, 1251, 1252 and 1255). wchar_t is UTF-32 on Linux, so a
// character outside the Basic Multilingual Plane is one wchar_t, not a surrogate pair.

#include "windows.h"

#include <algorithm>
#include <array>
#include <iconv.h>
#include <locale>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

// Narrow string literals are compiled with -fexec-charset=CP1252 (see code/CMakeLists.txt),
// so the ANSI code page corresponds to a western Windows installation.
constexpr UINT ansiCodePage = 1252;
constexpr UINT oemCodePage = 850;

UINT resolveCodePage(UINT codePage) {
  if (codePage == CP_ACP || codePage == CP_THREAD_ACP)
    return ansiCodePage;
  if (codePage == CP_OEMCP)
    return oemCodePage;
  return codePage;
}

bool isSingleByteCodePage(UINT codePage) {
  return codePage == 437 || codePage == 850 || codePage == 852 || codePage == 866 ||
         codePage == 874 || (codePage >= 1250 && codePage <= 1258) ||
         (codePage >= 28591 && codePage <= 28605);
}

struct SingleByteCodePage {
  std::array<wchar_t, 256> toWide{};
  std::unordered_map<wchar_t, unsigned char> toByte;
};

const SingleByteCodePage *singleByteCodePage(UINT codePage) {
  static std::mutex lock;
  static std::unordered_map<UINT, std::unique_ptr<SingleByteCodePage>> cache;

  std::lock_guard<std::mutex> guard(lock);
  auto cached = cache.find(codePage);
  if (cached != cache.end())
    return cached->second.get();

  std::unique_ptr<SingleByteCodePage> table;
  const std::string name = codePage >= 28591 ? "ISO-8859-" + std::to_string(codePage - 28590)
                                              : "CP" + std::to_string(codePage);
  iconv_t cd = iconv_open("WCHAR_T", name.c_str());
  if (cd != reinterpret_cast<iconv_t>(-1)) {
    table = std::make_unique<SingleByteCodePage>();
    for (int b = 0; b < 256; b++) {
      char in = static_cast<char>(b);
      wchar_t out = 0;
      char *inPtr = &in;
      char *outPtr = reinterpret_cast<char *>(&out);
      std::size_t inLeft = 1;
      std::size_t outLeft = sizeof(out);
      iconv(cd, nullptr, nullptr, nullptr, nullptr);
      // Bytes without a mapping (e.g. 0x81 in CP1252) decode to the code point of the
      // same value, as Windows does.
      if (iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft) == static_cast<std::size_t>(-1) || outLeft != 0)
        out = static_cast<wchar_t>(b);
      table->toWide[b] = out;
      table->toByte.emplace(out, static_cast<unsigned char>(b));
    }
    iconv_close(cd);
  }

  const SingleByteCodePage *result = table.get();
  cache.emplace(codePage, std::move(table));
  return result;
}

template<typename String>
int deliver(const String &result, typename String::value_type *dst, int dstLen) {
  const int needed = static_cast<int>(result.size());
  if (dstLen == 0)
    return needed;
  if (!dst || dstLen < needed)
    return 0; // ERROR_INSUFFICIENT_BUFFER on Windows
  std::copy(result.begin(), result.end(), dst);
  return needed;
}

const std::locale &userLocale() {
  static const std::locale locale = [] {
    for (const char *name : {"", "C.UTF-8"}) {
      try {
        return std::locale(name);
      }
      catch (const std::runtime_error &) {
      }
    }
    return std::locale::classic();
  }();
  return locale;
}

} // namespace

int MultiByteToWideChar(UINT codePage, DWORD /*flags*/, LPCSTR src, int srcLen, LPWSTR dst, int dstLen) {
  if (!src || srcLen == 0 || dstLen < 0)
    return 0;

  // A negative length includes the terminating null character in the conversion.
  const std::size_t length = srcLen < 0 ? std::strlen(src) + 1 : static_cast<std::size_t>(srcLen);
  codePage = resolveCodePage(codePage);

  std::wstring result;
  if (codePage == CP_UTF8) {
    result = meos_compat::utf8ToWide(src, length);
  }
  else {
    const SingleByteCodePage *table = isSingleByteCodePage(codePage) ? singleByteCodePage(codePage) : nullptr;
    if (!table)
      return 0;
    result.resize(length);
    for (std::size_t i = 0; i < length; i++)
      result[i] = table->toWide[static_cast<unsigned char>(src[i])];
  }
  return deliver(result, dst, dstLen);
}

int WideCharToMultiByte(UINT codePage, DWORD /*flags*/, LPCWSTR src, int srcLen, LPSTR dst, int dstLen,
                        LPCSTR defaultChar, LPBOOL usedDefaultChar) {
  if (!src || srcLen == 0 || dstLen < 0)
    return 0;

  const std::size_t length = srcLen < 0 ? std::wcslen(src) + 1 : static_cast<std::size_t>(srcLen);
  codePage = resolveCodePage(codePage);

  std::string result;
  if (codePage == CP_UTF8) {
    // Windows rejects a default character for UTF-8.
    if (defaultChar || usedDefaultChar)
      return 0;
    result = meos_compat::wideToUtf8(src, length);
  }
  else {
    const SingleByteCodePage *table = isSingleByteCodePage(codePage) ? singleByteCodePage(codePage) : nullptr;
    if (!table)
      return 0;
    const char replacement = defaultChar ? defaultChar[0] : '?';
    bool usedDefault = false;
    result.resize(length);
    for (std::size_t i = 0; i < length; i++) {
      auto mapped = table->toByte.find(src[i]);
      if (mapped != table->toByte.end()) {
        result[i] = static_cast<char>(mapped->second);
      }
      else {
        result[i] = replacement;
        usedDefault = true;
      }
    }
    if (usedDefaultChar)
      *usedDefaultChar = usedDefault ? TRUE : FALSE;
  }
  return deliver(result, dst, dstLen);
}

int CompareString(LCID /*locale*/, DWORD flags, LPCWSTR a, int lenA, LPCWSTR b, int lenB) {
  if (!a || !b)
    return 0;

  std::wstring left(a, lenA < 0 ? std::wcslen(a) : static_cast<std::size_t>(lenA));
  std::wstring right(b, lenB < 0 ? std::wcslen(b) : static_cast<std::size_t>(lenB));

  // Windows sorts linguistically according to the user locale; the user locale's
  // collation is the closest equivalent.
  const std::locale &locale = userLocale();
  if (flags & NORM_IGNORECASE) {
    const auto &ctype = std::use_facet<std::ctype<wchar_t>>(locale);
    ctype.tolower(left.data(), left.data() + left.size());
    ctype.tolower(right.data(), right.data() + right.size());
  }

  const auto &collate = std::use_facet<std::collate<wchar_t>>(locale);
  const int order = collate.compare(left.data(), left.data() + left.size(),
                                    right.data(), right.data() + right.size());
  return order < 0 ? CSTR_LESS_THAN : order > 0 ? CSTR_GREATER_THAN : CSTR_EQUAL;
}

BOOL IsCharAlphaNumeric(WCHAR ch) {
  return std::isalnum(ch, userLocale()) ? TRUE : FALSE;
}

DWORD CharLowerBuff(LPWSTR buffer, DWORD length) {
  if (!buffer)
    return 0;
  std::use_facet<std::ctype<wchar_t>>(userLocale()).tolower(buffer, buffer + length);
  return length;
}

DWORD CharUpperBuff(LPWSTR buffer, DWORD length) {
  if (!buffer)
    return 0;
  std::use_facet<std::ctype<wchar_t>>(userLocale()).toupper(buffer, buffer + length);
  return length;
}

// As on Windows, a pointer value below 0x10000 is a single character to convert.
LPWSTR CharLower(LPWSTR text) {
  const std::ctype<wchar_t> &ctype = std::use_facet<std::ctype<wchar_t>>(userLocale());
  if ((std::uintptr_t)text >> 16 == 0)
    return (LPWSTR)(std::uintptr_t)ctype.tolower((wchar_t)(std::uintptr_t)text);
  ctype.tolower(text, text + std::wcslen(text));
  return text;
}

int lstrcmpi(LPCWSTR a, LPCWSTR b) {
  return CompareString(LOCALE_USER_DEFAULT, NORM_IGNORECASE, a, -1, b, -1) - CSTR_EQUAL;
}

// Visible only in debug builds, where Windows would show it in the debugger output.
void OutputDebugString(LPCWSTR text) {
#ifdef _DEBUG
  if (text)
    std::fputs(meos_compat::wideToUtf8(text).c_str(), stderr);
#else
  (void)text;
#endif
}

void OutputDebugStringA(LPCSTR text) {
  if (!text)
    return;
  // Narrow strings are in the ANSI code page (see -fexec-charset in code/CMakeLists.txt).
  const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
  if (length <= 0)
    return;
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), length);
  OutputDebugString(wide.c_str());
}
