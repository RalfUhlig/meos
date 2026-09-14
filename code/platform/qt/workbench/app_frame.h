/************************************************************************
    MeOS - Orienteering Software
    Linux port: the application frame of the GUI workbench and the load test.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// meos.cpp defines the globals and helper functions the rest of MeOS refers to
// (gdi_main, gEvent, lang, getUserFile, createExtraWindow, ...). Programs that link
// MeOS without meos.cpp get them from app_frame.cpp instead. The implementations
// follow meos.cpp, without tabs, auto tasks and a main window of their own. They
// only use the Win32 API, so they build on Windows as well.

#pragma once

#include <string>
#include <vector>

#include <windows.h>

class gdioutput;

namespace app_frame {

// Initializes MeOS as WinMain in meos.cpp does (status order, language, string cache,
// the main gdioutput and oEvent, properties, language tables, list definitions).
// Settings and other user files are kept in the subfolder dataFolder of the user's
// application data folder, so that a tool does not change the settings of MeOS.
// Returns false and reports on stderr if the event cannot be created.
bool initialize(HINSTANCE instance, const wchar_t *dataFolder, const std::wstring &language = L"English");

// Registers the window class of MeOS work spaces (WorkSpaceWndProc).
void registerWorkSpaceClass(HINSTANCE instance);
const wchar_t *workSpaceClassName();

// Installs the keyboard hook of MeOS (Tab, Enter, Escape, arrows, Ctrl shortcuts).
void installKeyboardHook();

// Call every 100 ms from a WM_TIMER of the main window, as meos.cpp does through
// AutoTask::interfaceTimeout: updates timers and removes expired info boxes.
void interfaceTimeout();

// Destroys extra windows, the event and the language tables, as at the end of WinMain.
void shutdown();

// The work space procedure of meos.cpp: forwards messages to the gdioutput of the
// window (index in GWLP_USERDATA) and scrolls.
LRESULT CALLBACK WorkSpaceWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

} // namespace app_frame

extern std::vector<gdioutput *> gdi_extra;
