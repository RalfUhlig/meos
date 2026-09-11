// Linux port: stand-in for the Windows SDK header <commctrl.h>.
// Common controls are replaced by the Qt backend; only the types that MeOS headers
// use as data members are declared here.
#pragma once

#include "windows.h"

typedef struct tagTOOLINFOW {
  UINT      cbSize;
  UINT      uFlags;
  HWND      hwnd;
  UINT_PTR  uId;
  RECT      rect;
  HINSTANCE hinst;
  LPWSTR    lpszText;
  LPARAM    lParam;
  void     *lpReserved;
} TTTOOLINFOW, *LPTTTOOLINFOW;

typedef TTTOOLINFOW TOOLINFOW;
typedef TTTOOLINFOW TOOLINFO;
