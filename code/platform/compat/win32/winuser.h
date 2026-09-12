/************************************************************************
    MeOS - Orienteering Software
    Linux port: stand-in for the Windows SDK header <winuser.h>.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// The part of the window manager API (USER32) that MeOS uses: windows and window
// classes, messages, the standard controls, timers, hooks, menus, the clipboard and
// screen information. Included by windows.h. Constants have their Windows values.
//
// Implemented by the Qt backend in code/platform/qt (stage 1.2); the file named in
// each section does not exist yet.

#pragma once

#include "windows.h"

/* ---------------------------------------------------------------------
   Callback types and structures
   --------------------------------------------------------------------- */
typedef void (CALLBACK *TIMERPROC)(HWND window, UINT message, UINT_PTR id, DWORD time);
typedef LRESULT (CALLBACK *HOOKPROC)(int code, WPARAM wParam, LPARAM lParam);
typedef BOOL (CALLBACK *MONITORENUMPROC)(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data);

typedef struct tagWNDCLASSEXW {
  UINT      cbSize;
  UINT      style;
  WNDPROC   lpfnWndProc;
  int       cbClsExtra;
  int       cbWndExtra;
  HINSTANCE hInstance;
  HICON     hIcon;
  HCURSOR   hCursor;
  HBRUSH    hbrBackground;
  LPCWSTR   lpszMenuName;
  LPCWSTR   lpszClassName;
  HICON     hIconSm;
} WNDCLASSEX;

typedef struct tagPAINTSTRUCT {
  HDC  hdc;
  BOOL fErase;
  RECT rcPaint;
  BOOL fRestore;
  BOOL fIncUpdate;
  BYTE rgbReserved[32];
} PAINTSTRUCT;

typedef struct tagSCROLLINFO {
  UINT cbSize;
  UINT fMask;
  int  nMin;
  int  nMax;
  UINT nPage;
  int  nPos;
  int  nTrackPos;
} SCROLLINFO;

typedef struct tagWINDOWPLACEMENT {
  UINT  length;
  UINT  flags;
  UINT  showCmd;
  POINT ptMinPosition;
  POINT ptMaxPosition;
  RECT  rcNormalPosition;
} WINDOWPLACEMENT;

struct tagTPMPARAMS;
typedef struct tagTPMPARAMS *LPTPMPARAMS;

/* ---------------------------------------------------------------------
   Message parameters
   --------------------------------------------------------------------- */
#define MAKEWPARAM(low, high) ((WPARAM)(DWORD)MAKELONG(low, high))
#define MAKELPARAM(low, high) ((LPARAM)(DWORD)MAKELONG(low, high))

/* ---------------------------------------------------------------------
   Messages
   --------------------------------------------------------------------- */
#define WM_CREATE            0x0001
#define WM_DESTROY           0x0002
#define WM_SIZE              0x0005
#define WM_ACTIVATE          0x0006
#define WM_SETREDRAW         0x000B
#define WM_PAINT             0x000F
#define WM_CLOSE             0x0010
#define WM_SETFONT           0x0030
#define WM_WINDOWPOSCHANGED  0x0047
#define WM_NCACTIVATE        0x0086
#define WM_KEYDOWN           0x0100
#define WM_CHAR              0x0102
#define WM_COMMAND           0x0111
#define WM_VSCROLL           0x0115
#define WM_CTLCOLOREDIT      0x0133
#define WM_MOUSEMOVE         0x0200
#define WM_LBUTTONDOWN       0x0201
#define WM_LBUTTONUP         0x0202
#define WM_LBUTTONDBLCLK     0x0203
#define WM_RBUTTONDOWN       0x0204
#define WM_RBUTTONUP         0x0205
#define WM_MBUTTONDOWN       0x0207
#define WM_MBUTTONUP         0x0208
#define WM_MOUSEWHEEL        0x020A
#define WM_PASTE             0x0302
#define WM_USER              0x0400

/* ---------------------------------------------------------------------
   Windows and window classes: code/platform/qt/win32_window.cpp
   --------------------------------------------------------------------- */
#define WS_OVERLAPPED       0x00000000
#define WS_POPUP            0x80000000
#define WS_CHILD            0x40000000
#define WS_VISIBLE          0x10000000
#define WS_CLIPSIBLINGS     0x04000000
#define WS_CLIPCHILDREN     0x02000000
#define WS_CAPTION          0x00C00000
#define WS_BORDER           0x00800000
#define WS_VSCROLL          0x00200000
#define WS_HSCROLL          0x00100000
#define WS_SYSMENU          0x00080000
#define WS_THICKFRAME       0x00040000
#define WS_MINIMIZEBOX      0x00020000
#define WS_MAXIMIZEBOX      0x00010000
#define WS_TABSTOP          0x00010000
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

#define WS_EX_TOPMOST     0x00000008
#define WS_EX_TOOLWINDOW  0x00000080
#define WS_EX_CLIENTEDGE  0x00000200

#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002

#define CW_USEDEFAULT ((int)0x80000000)

#define GWLP_WNDPROC   (-4)
#define GWLP_HINSTANCE (-6)
#define GWL_STYLE      (-16)
#define GWL_EXSTYLE    (-20)
#define GWLP_USERDATA  (-21)

#define SW_HIDE       0
#define SW_SHOWNORMAL 1
#define SW_NORMAL     1
#define SW_MAXIMIZE   3
#define SW_SHOW       5

#define HWND_TOP     ((HWND)0)
#define HWND_TOPMOST ((HWND)-1)

#define SWP_NOSIZE     0x0001
#define SWP_NOMOVE     0x0002
#define SWP_NOZORDER   0x0004
#define SWP_NOCOPYBITS 0x0100

#define RDW_INVALIDATE  0x0001
#define RDW_ERASE       0x0004
#define RDW_ALLCHILDREN 0x0080
#define RDW_FRAME       0x0400

ATOM RegisterClassEx(const WNDCLASSEX *windowClass);
HWND CreateWindowEx(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y,
                    int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param);
inline HWND CreateWindow(LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y, int width,
                         int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
  return CreateWindowEx(0, className, windowName, style, x, y, width, height, parent, menu, instance, param);
}
BOOL DestroyWindow(HWND window);
LRESULT DefWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CallWindowProc(WNDPROC windowProc, HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LONG GetWindowLong(HWND window, int index);
LONG SetWindowLong(HWND window, int index, LONG value);
LONG_PTR GetWindowLongPtr(HWND window, int index);
LONG_PTR SetWindowLongPtr(HWND window, int index, LONG_PTR value);
BOOL SetWindowText(HWND window, LPCWSTR text);
int GetWindowText(HWND window, LPWSTR buffer, int maxCount);
int GetWindowTextLength(HWND window);
HWND GetDlgItem(HWND dialog, int id);
BOOL ShowWindow(HWND window, int command);
BOOL IsWindowVisible(HWND window);
BOOL EnableWindow(HWND window, BOOL enable);
BOOL IsWindowEnabled(HWND window);
BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL SetWindowPos(HWND window, HWND insertAfter, int x, int y, int width, int height, UINT flags);
BOOL GetWindowPlacement(HWND window, WINDOWPLACEMENT *placement);
BOOL SetWindowPlacement(HWND window, const WINDOWPLACEMENT *placement);
BOOL GetClientRect(HWND window, LPRECT rect);
BOOL GetWindowRect(HWND window, LPRECT rect);
BOOL ClientToScreen(HWND window, LPPOINT point);
BOOL ScreenToClient(HWND window, LPPOINT point);
HWND WindowFromPoint(POINT point);
HWND GetDesktopWindow();
BOOL SetForegroundWindow(HWND window);
HWND SetActiveWindow(HWND window);
HWND SetFocus(HWND window);
HWND GetFocus();
HWND SetCapture(HWND window);
BOOL ReleaseCapture();
HWND GetCapture();

/* ---------------------------------------------------------------------
   Messages, timers and hooks: code/platform/qt/win32_message.cpp
   --------------------------------------------------------------------- */
#define WH_KEYBOARD   2
#define WH_CBT        5
#define HCBT_ACTIVATE 5

LRESULT SendMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
BOOL PostMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC timerProc);
BOOL KillTimer(HWND window, UINT_PTR id);
HHOOK SetWindowsHookEx(int hookType, HOOKPROC hookProc, HINSTANCE module, DWORD threadId);
BOOL UnhookWindowsHookEx(HHOOK hook);
LRESULT CallNextHookEx(HHOOK hook, int code, WPARAM wParam, LPARAM lParam);

/* ---------------------------------------------------------------------
   Painting and scrolling: code/platform/qt/win32_canvas.cpp
   --------------------------------------------------------------------- */
#define SB_HORZ 0
#define SB_VERT 1

#define SB_LINEUP     0
#define SB_LINEDOWN   1
#define SB_PAGEUP     2
#define SB_PAGEDOWN   3
#define SB_THUMBTRACK 5
#define SB_ENDSCROLL  8

#define SIF_RANGE    0x0001
#define SIF_PAGE     0x0002
#define SIF_POS      0x0004
#define SIF_TRACKPOS 0x0010
#define SIF_ALL      (SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS)

#define SW_SCROLLCHILDREN 0x0001
#define SW_INVALIDATE     0x0002
#define SW_SMOOTHSCROLL   0x0010

HDC BeginPaint(HWND window, PAINTSTRUCT *paint);
BOOL EndPaint(HWND window, const PAINTSTRUCT *paint);
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
BOOL InvalidateRect(HWND window, const RECT *rect, BOOL erase);
BOOL UpdateWindow(HWND window);
BOOL RedrawWindow(HWND window, const RECT *updateRect, HRGN updateRegion, UINT flags);
int SetScrollInfo(HWND window, int bar, const SCROLLINFO *info, BOOL redraw);
int ScrollWindowEx(HWND window, int dx, int dy, const RECT *scrollRect, const RECT *clipRect,
                   HRGN updateRegion, LPRECT updateRect, UINT flags);

/* ---------------------------------------------------------------------
   Standard controls: code/platform/qt/win32_controls.cpp
   The controls are created with CreateWindowEx and driven by SendMessage.
   --------------------------------------------------------------------- */
#define IMAGE_BITMAP 0
#define LR_CREATEDIBSECTION 0x00002000

// BUTTON
#define BS_PUSHBUTTON    0x00000000
#define BS_DEFPUSHBUTTON 0x00000001
#define BS_CHECKBOX      0x00000002
#define BS_AUTOCHECKBOX  0x00000003
#define BS_BITMAP        0x00000080
#define BS_PUSHLIKE      0x00001000
#define BS_MULTILINE     0x00002000
#define BS_NOTIFY        0x00004000

#define BM_GETCHECK 0x00F0
#define BM_SETCHECK 0x00F1
#define BM_SETIMAGE 0x00F7

#define BST_UNCHECKED 0x0000
#define BST_CHECKED   0x0001

#define BN_CLICKED   0
#define BN_SETFOCUS  6
#define BN_KILLFOCUS 7

// EDIT
#define ES_MULTILINE   0x0004
#define ES_PASSWORD    0x0020
#define ES_AUTOVSCROLL 0x0040
#define ES_AUTOHSCROLL 0x0080

#define EM_SETSEL          0x00B1
#define EM_REPLACESEL      0x00C2
#define EM_LIMITTEXT       0x00C5
#define EM_SETPASSWORDCHAR 0x00CC

#define EN_SETFOCUS  0x0100
#define EN_KILLFOCUS 0x0200
#define EN_CHANGE    0x0300

// COMBOBOX
#define CBS_DROPDOWN     0x0002
#define CBS_DROPDOWNLIST 0x0003
#define CBS_AUTOHSCROLL  0x0040

#define CB_ERR (-1)

#define CB_ADDSTRING     0x0143
#define CB_DELETESTRING  0x0144
#define CB_GETCOUNT      0x0146
#define CB_GETCURSEL     0x0147
#define CB_GETLBTEXT     0x0148
#define CB_INSERTSTRING  0x014A
#define CB_RESETCONTENT  0x014B
#define CB_FINDSTRING    0x014C
#define CB_SETCURSEL     0x014E
#define CB_GETITEMDATA   0x0150
#define CB_SETITEMDATA   0x0151
#define CB_INITSTORAGE   0x0161

#define CBN_SELCHANGE  1
#define CBN_SETFOCUS   3
#define CBN_KILLFOCUS  4
#define CBN_EDITCHANGE 5

// LISTBOX
#define LBS_NOTIFY       0x0001
#define LBS_MULTIPLESEL  0x0008
#define LBS_USETABSTOPS  0x0080

#define LB_ERR (-1)

#define LB_INSERTSTRING  0x0181
#define LB_DELETESTRING  0x0182
#define LB_RESETCONTENT  0x0184
#define LB_SETSEL        0x0185
#define LB_SETCURSEL     0x0186
#define LB_GETSEL        0x0187
#define LB_GETCURSEL     0x0188
#define LB_GETTEXT       0x0189
#define LB_GETCOUNT      0x018B
#define LB_GETTOPINDEX   0x018E
#define LB_FINDSTRING    0x018F
#define LB_SETTABSTOPS   0x0192
#define LB_SETTOPINDEX   0x0197
#define LB_GETITEMDATA   0x0199
#define LB_SETITEMDATA   0x019A
#define LB_INITSTORAGE   0x01A8

#define LBN_SELCHANGE  1
#define LBN_DBLCLK     2
#define LBN_SETFOCUS   4
#define LBN_KILLFOCUS  5

/* ---------------------------------------------------------------------
   Text output with the window manager: code/platform/qt/win32_text.cpp
   --------------------------------------------------------------------- */
#define DT_LEFT         0x00000000
#define DT_CENTER       0x00000001
#define DT_RIGHT        0x00000002
#define DT_WORDBREAK    0x00000010
#define DT_SINGLELINE   0x00000020
#define DT_NOCLIP       0x00000100
#define DT_CALCRECT     0x00000400
#define DT_NOPREFIX     0x00000800
#define DT_END_ELLIPSIS 0x00008000

int DrawText(HDC dc, LPCWSTR text, int length, LPRECT rect, UINT format);

/* ---------------------------------------------------------------------
   Message boxes and menus: code/platform/qt/win32_dialogs.cpp
   --------------------------------------------------------------------- */
#define MB_OK                0x00000000
#define MB_OKCANCEL          0x00000001
#define MB_YESNOCANCEL       0x00000003
#define MB_YESNO             0x00000004
#define MB_ICONQUESTION      0x00000020
#define MB_ICONEXCLAMATION   0x00000030
#define MB_ICONWARNING       0x00000030
#define MB_ICONINFORMATION   0x00000040

#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7

#define MF_STRING    0x00000000
#define MF_SEPARATOR 0x00000800

#define TPM_LEFTALIGN 0x0000
#define TPM_TOPALIGN  0x0000
#define TPM_NONOTIFY  0x0080
#define TPM_RETURNCMD 0x0100

int MessageBox(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type);
inline int MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type) {
  return MessageBox(owner, text, caption, type);
}
BOOL MessageBeep(UINT type);
HMENU CreatePopupMenu();
BOOL AppendMenu(HMENU menu, UINT flags, UINT_PTR idNewItem, LPCWSTR newItem);
BOOL TrackPopupMenuEx(HMENU menu, UINT flags, int x, int y, HWND window, LPTPMPARAMS params);
BOOL DestroyMenu(HMENU menu);

/* ---------------------------------------------------------------------
   Clipboard: code/platform/qt/win32_clipboard.cpp
   --------------------------------------------------------------------- */
#define CF_TEXT        1
#define CF_UNICODETEXT 13

BOOL OpenClipboard(HWND owner);
BOOL CloseClipboard();
BOOL EmptyClipboard();
HANDLE SetClipboardData(UINT format, HANDLE memory);
HANDLE GetClipboardData(UINT format);
UINT RegisterClipboardFormat(LPCWSTR formatName);

/* ---------------------------------------------------------------------
   Cursors, bitmaps, system metrics and colours, monitors:
   code/platform/qt/win32_screen.cpp
   --------------------------------------------------------------------- */
#define IDC_ARROW MAKEINTRESOURCE(32512)
#define IDC_WAIT  MAKEINTRESOURCE(32514)
#define IDC_HAND  MAKEINTRESOURCE(32649)

#define SM_CXSCREEN        0
#define SM_CYSCREEN        1
#define SM_CXEDGE          45
#define SM_CYEDGE          46
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79

#define COLOR_ACTIVECAPTION 2
#define COLOR_WINDOW        5
#define COLOR_3DFACE        15
#define COLOR_BTNFACE       15
#define COLOR_GRAYTEXT      17
#define COLOR_3DHIGHLIGHT   20
#define COLOR_INFOTEXT      23
#define COLOR_INFOBK        24

HCURSOR LoadCursor(HINSTANCE instance, LPCWSTR cursorName);
HCURSOR SetCursor(HCURSOR cursor);
HBITMAP LoadBitmap(HINSTANCE instance, LPCWSTR bitmapName);
int GetSystemMetrics(int index);
DWORD GetSysColor(int index);
BOOL EnumDisplayMonitors(HDC dc, LPCRECT clip, MONITORENUMPROC enumProc, LPARAM data);

/* ---------------------------------------------------------------------
   Rectangles (no operating system state, implemented here)
   --------------------------------------------------------------------- */
inline BOOL PtInRect(const RECT *rect, POINT point) {
  return point.x >= rect->left && point.x < rect->right && point.y >= rect->top && point.y < rect->bottom;
}

inline BOOL IsRectEmpty(const RECT *rect) {
  return rect->right <= rect->left || rect->bottom <= rect->top;
}

inline BOOL OffsetRect(LPRECT rect, int dx, int dy) {
  rect->left += dx;
  rect->right += dx;
  rect->top += dy;
  rect->bottom += dy;
  return TRUE;
}

inline BOOL EqualRect(const RECT *a, const RECT *b) {
  return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

// As on Windows, an empty intersection yields an empty rectangle at the origin.
inline BOOL IntersectRect(LPRECT dst, const RECT *a, const RECT *b) {
  RECT r = {std::max(a->left, b->left), std::max(a->top, b->top), std::min(a->right, b->right),
            std::min(a->bottom, b->bottom)};
  if (IsRectEmpty(a) || IsRectEmpty(b) || IsRectEmpty(&r)) {
    *dst = RECT{0, 0, 0, 0};
    return FALSE;
  }
  *dst = r;
  return TRUE;
}
