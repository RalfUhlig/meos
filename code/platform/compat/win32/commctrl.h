// Linux port: stand-in for the Windows SDK header <commctrl.h>.
// The common controls MeOS uses (tooltips, the table toolbar, image lists) are
// implemented by the Qt backend in code/platform/qt/win32_commctrl.cpp (stage 1.2).
// The tab control follows with meos.cpp (stage 1.3). Constants have their Windows
// values.

#pragma once

#include "windows.h"

/* Initialisation */
void InitCommonControls();

#define CCM_FIRST      0x2000
#define CCM_SETVERSION (CCM_FIRST + 0x7)

#define HINST_COMMCTRL ((HINSTANCE)-1)

/* Tooltips */
#define TOOLTIPS_CLASS L"tooltips_class32"

#define TTS_ALWAYSTIP 0x01

#define TTF_IDISHWND 0x0001
#define TTF_SUBCLASS 0x0010

#define TTM_RELAYEVENT      (WM_USER + 7)
#define TTM_SETMAXTIPWIDTH  (WM_USER + 24)
#define TTM_ADDTOOLW        (WM_USER + 50)
#define TTM_DELTOOLW        (WM_USER + 51)
#define TTM_NEWTOOLRECTW    (WM_USER + 52)
#define TTM_UPDATETIPTEXTW  (WM_USER + 57)
#define TTM_DELTOOL         TTM_DELTOOLW

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

/* Image lists */
struct _IMAGELIST;
typedef struct _IMAGELIST *HIMAGELIST;

#define ILC_MASK    0x00000001
#define ILC_COLOR24 0x00000018

#define CLR_NONE    0xFFFFFFFF
#define CLR_DEFAULT 0xFF000000

HIMAGELIST ImageList_Create(int cx, int cy, UINT flags, int initial, int grow);
HIMAGELIST ImageList_LoadImage(HINSTANCE instance, LPCWSTR bitmap, int cx, int grow, COLORREF mask,
                               UINT type, UINT flags);
BOOL ImageList_Destroy(HIMAGELIST imageList);

/* Toolbar */
#define TOOLBARCLASSNAME L"ToolbarWindow32"

#define TBSTYLE_TOOLTIPS 0x0100
#define BTNS_AUTOSIZE    0x0010
#define TBSTATE_ENABLED  0x04

#define TB_BUTTONSTRUCTSIZE (WM_USER + 30)
#define TB_AUTOSIZE         (WM_USER + 33)
#define TB_SETIMAGELIST     (WM_USER + 48)
#define TB_LOADIMAGES       (WM_USER + 50)
#define TB_GETBUTTONSIZE    (WM_USER + 58)
#define TB_SETMAXTEXTROWS   (WM_USER + 60)
#define TB_ADDBUTTONS       (WM_USER + 68)

#define IDB_STD_LARGE_COLOR 1

#define STD_COPY  1
#define STD_PASTE 2
#define STD_PRINT 14

typedef struct _TBBUTTON {
  int       iBitmap;
  int       idCommand;
  BYTE      fsState;
  BYTE      fsStyle;
  BYTE      bReserved[6];
  DWORD_PTR dwData;
  INT_PTR   iString;
} TBBUTTON;

/* Buttons */
#define BCM_FIRST        0x1600
#define BCM_GETIDEALSIZE (BCM_FIRST + 0x0001)

#define Button_GetIdealSize(hwnd, psize) ((BOOL)SendMessage((hwnd), BCM_GETIDEALSIZE, 0, (LPARAM)(psize)))

/* Tab control */
#define TCM_FIRST     0x1300
#define TCM_SETCURSEL (TCM_FIRST + 12)

#define TabCtrl_SetCurSel(hwnd, i) ((int)SendMessage((hwnd), TCM_SETCURSEL, (WPARAM)(i), 0))
