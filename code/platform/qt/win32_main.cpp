/************************************************************************
    MeOS - Orienteering Software
    Linux port: main() of Win32 programs built on the Qt backend.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Linked into programs that define WinMain. Kept apart from win32_app.cpp so that
// programs with their own main(), such as the self test, can use the backend.

#include "win32_ui.h"

namespace {

// The arguments after the program name, separated by blanks and in the ANSI code
// page, as Windows passes the command line to WinMain.
std::vector<char> commandLine(int argc, char **argv) {
  std::string line;
  for (int i = 1; i < argc; i++) {
    const std::wstring wide = meos_compat::utf8ToWide(argv[i]);
    const int size = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), int(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string argument(std::size_t(std::max(size, 0)), '\0');
    if (size > 0)
      WideCharToMultiByte(CP_ACP, 0, wide.c_str(), int(wide.size()), &argument[0], size, nullptr, nullptr);
    if (argument.find(' ') != std::string::npos)
      argument = '"' + argument + '"';
    if (!line.empty())
      line += ' ';
    line += argument;
  }
  std::vector<char> buffer(line.begin(), line.end());
  buffer.push_back('\0');
  return buffer;
}

} // namespace

int main(int argc, char **argv) {
  const std::unique_ptr<QApplication> app = meos_qt::createApplication(argc, argv);
  std::vector<char> arguments = commandLine(argc, argv);
  return WinMain(meos_qt::applicationInstance(), nullptr, arguments.data(), SW_SHOWDEFAULT);
}
