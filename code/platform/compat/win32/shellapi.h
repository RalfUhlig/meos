// Linux port: stand-in for the Windows SDK header <shellapi.h>.
// ShellExecute opens documents and URLs with the desktop's default application; it is
// implemented by the Qt backend in code/platform/qt/win32_dialogs.cpp (stage 1.2,
// not yet available). As on Windows, a return value greater than 32 means success.

#pragma once

#include "windows.h"

HINSTANCE ShellExecute(HWND owner, LPCWSTR operation, LPCWSTR file, LPCWSTR parameters, LPCWSTR directory,
                       INT showCommand);
