// Linux port: stand-in for the Windows SDK header <commdlg.h>.
// The common dialogs MeOS uses (open/save file, colour, print setup) are implemented
// by the Qt backend in code/platform/qt/win32_dialogs.cpp. Until stage 3 there is no
// printer: PrintDlg and PageSetupDlg fail with PDERR_NODEFAULTPRN, as on a Windows
// system without printers.
// The spelling "CommDlg.h" used by some sources is resolved by the case alias
// headers generated in code/CMakeLists.txt, not by a second file here.

#pragma once

#include "windows.h"

typedef UINT_PTR (CALLBACK *LPOFNHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR (CALLBACK *LPCCHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR (CALLBACK *LPPRINTHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR (CALLBACK *LPSETUPHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR (CALLBACK *LPPAGESETUPHOOK)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR (CALLBACK *LPPAGEPAINTHOOK)(HWND, UINT, WPARAM, LPARAM);

#define CDERR_STRUCTSIZE     0x0001
#define CDERR_INITIALIZATION 0x0002
#define PDERR_NODEFAULTPRN   0x1008
#define FNERR_BUFFERTOOSMALL 0x3003

DWORD CommDlgExtendedError();

/* Open and save file */
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_HIDEREADONLY    0x00000004
#define OFN_PATHMUSTEXIST   0x00000800
#define OFN_FILEMUSTEXIST   0x00001000

typedef struct tagOFNW {
  DWORD         lStructSize;
  HWND          hwndOwner;
  HINSTANCE     hInstance;
  LPCWSTR       lpstrFilter;
  LPWSTR        lpstrCustomFilter;
  DWORD         nMaxCustFilter;
  DWORD         nFilterIndex;
  LPWSTR        lpstrFile;
  DWORD         nMaxFile;
  LPWSTR        lpstrFileTitle;
  DWORD         nMaxFileTitle;
  LPCWSTR       lpstrInitialDir;
  LPCWSTR       lpstrTitle;
  DWORD         Flags;
  WORD          nFileOffset;
  WORD          nFileExtension;
  LPCWSTR       lpstrDefExt;
  LPARAM        lCustData;
  LPOFNHOOKPROC lpfnHook;
  LPCWSTR       lpTemplateName;
  void         *pvReserved;
  DWORD         dwReserved;
  DWORD         FlagsEx;
} OPENFILENAME, *LPOPENFILENAME;

BOOL GetOpenFileName(LPOPENFILENAME ofn);
BOOL GetSaveFileName(LPOPENFILENAME ofn);

/* Colour */
#define CC_RGBINIT 0x00000001

typedef struct tagCHOOSECOLORW {
  DWORD        lStructSize;
  HWND         hwndOwner;
  HWND         hInstance;
  COLORREF     rgbResult;
  COLORREF    *lpCustColors;
  DWORD        Flags;
  LPARAM       lCustData;
  LPCCHOOKPROC lpfnHook;
  LPCWSTR      lpTemplateName;
} CHOOSECOLOR, *LPCHOOSECOLOR;

BOOL ChooseColor(LPCHOOSECOLOR cc);

/* Printing */
#define PD_PRINTSETUP                 0x00000040
#define PD_RETURNDC                   0x00000100
#define PD_RETURNDEFAULT              0x00000400
#define PD_USEDEVMODECOPIESANDCOLLATE 0x00040000

#define PSD_MARGINS                   0x00000002
#define PSD_INHUNDREDTHSOFMILLIMETERS 0x00000008
#define PSD_ENABLEPAGEPAINTHOOK       0x00040000

typedef struct tagDEVNAMES {
  WORD wDriverOffset;
  WORD wDeviceOffset;
  WORD wOutputOffset;
  WORD wDefault;
} DEVNAMES, *LPDEVNAMES;

typedef struct tagPDW {
  DWORD           lStructSize;
  HWND            hwndOwner;
  HGLOBAL         hDevMode;
  HGLOBAL         hDevNames;
  HDC             hDC;
  DWORD           Flags;
  WORD            nFromPage;
  WORD            nToPage;
  WORD            nMinPage;
  WORD            nMaxPage;
  WORD            nCopies;
  HINSTANCE       hInstance;
  LPARAM          lCustData;
  LPPRINTHOOKPROC lpfnPrintHook;
  LPSETUPHOOKPROC lpfnSetupHook;
  LPCWSTR         lpPrintTemplateName;
  LPCWSTR         lpSetupTemplateName;
  HGLOBAL         hPrintTemplate;
  HGLOBAL         hSetupTemplate;
} PRINTDLG, *LPPRINTDLG;

typedef struct tagPSDW {
  DWORD           lStructSize;
  HWND            hwndOwner;
  HGLOBAL         hDevMode;
  HGLOBAL         hDevNames;
  DWORD           Flags;
  POINT           ptPaperSize;
  RECT            rtMinMargin;
  RECT            rtMargin;
  HINSTANCE       hInstance;
  LPARAM          lCustData;
  LPPAGESETUPHOOK lpfnPageSetupHook;
  LPPAGEPAINTHOOK lpfnPagePaintHook;
  LPCWSTR         lpPageSetupTemplateName;
  HGLOBAL         hPageSetupTemplate;
} PAGESETUPDLG, *LPPAGESETUPDLG;

BOOL PrintDlg(LPPRINTDLG pd);
BOOL PageSetupDlg(LPPAGESETUPDLG psd);
