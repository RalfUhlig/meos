/************************************************************************
    MeOS - Orienteering Software
    Linux port: portable file paths for the standard library.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#pragma once

#include <algorithm>
#include <filesystem>
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
