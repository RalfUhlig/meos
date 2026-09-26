/************************************************************************
    MeOS - Orienteering Software
    Linux port: portable file paths and text lines for the standard library.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#pragma once

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <istream>
#include <string>

// Converts a MeOS file name to std::filesystem::path, for use with std::ifstream and
// std::ofstream (MSVC also accepts wide strings there directly, other compilers do not).
// MeOS builds paths with '\' as separator; outside Windows it is replaced by '/'.
inline std::filesystem::path meosPath(const std::wstring &file) {
#ifdef _WIN32
  return std::filesystem::path(file);
#else
  std::wstring native(file);
  std::replace(native.begin(), native.end(), L'\\', L'/');
  return std::filesystem::path(native);
#endif
}

inline std::filesystem::path meosPath(const wchar_t *file) {
  return meosPath(std::wstring(file ? file : L""));
}

// Reads a line like std::getline. On Windows the text mode of the stream turns
// "\r\n" into "\n"; elsewhere the '\r' of a Windows line end is dropped here, so
// a file written on Windows reads the same on both platforms.
template <class Char, class Traits, class Alloc>
std::basic_istream<Char, Traits> &meosGetline(std::basic_istream<Char, Traits> &in,
                                              std::basic_string<Char, Traits, Alloc> &line) {
  std::getline(in, line);
#ifndef _WIN32
  if (!line.empty() && line.back() == Char('\r'))
    line.pop_back();
#endif
  return in;
}

// The same for istream::getline into a character buffer.
inline std::istream &meosGetline(std::istream &in, char *buffer, std::streamsize size) {
  in.getline(buffer, size);
#ifndef _WIN32
  const std::size_t length = std::strlen(buffer);
  if (length > 0 && buffer[length - 1] == '\r')
    buffer[length - 1] = 0;
#endif
  return in;
}
