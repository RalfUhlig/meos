// Linux port: stand-in for the Windows SDK header <shlobj.h>.
// The folder browser (SHBrowseForFolder) is implemented by the Qt backend in
// code/platform/qt/win32_dialogs.cpp (stage 1.2, not yet available). An item ID list
// is opaque to MeOS: it is only passed to SHGetPathFromIDList and freed through the
// task allocator from SHGetMalloc.

#pragma once

#include "objbase.h"
#include "windows.h"

struct _ITEMIDLIST;
typedef struct _ITEMIDLIST ITEMIDLIST;
typedef ITEMIDLIST *LPITEMIDLIST;
typedef const ITEMIDLIST *LPCITEMIDLIST;

typedef int (CALLBACK *BFFCALLBACK)(HWND window, UINT message, LPARAM lParam, LPARAM data);

#define BIF_RETURNONLYFSDIRS 0x00000001
#define BIF_EDITBOX          0x00000010
#define BIF_NEWDIALOGSTYLE   0x00000040

typedef struct _browseinfoW {
  HWND          hwndOwner;
  LPCITEMIDLIST pidlRoot;
  LPWSTR        pszDisplayName;
  LPCWSTR       lpszTitle;
  UINT          ulFlags;
  BFFCALLBACK   lpfn;
  LPARAM        lParam;
  int           iImage;
} BROWSEINFO;

LPITEMIDLIST SHBrowseForFolder(BROWSEINFO *browseInfo);
BOOL SHGetPathFromIDList(LPCITEMIDLIST idList, LPWSTR path);
HRESULT SHGetMalloc(LPMALLOC *malloc);
