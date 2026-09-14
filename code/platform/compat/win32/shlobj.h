// Linux port: stand-in for the Windows SDK header <shlobj.h>.
// The folder browser (SHBrowseForFolder) is implemented by the Qt backend in
// code/platform/qt/win32_dialogs.cpp. An item ID list
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

// Special folders, mapped to the XDG folders of the desktop (QStandardPaths):
// CSIDL_APPDATA is the user's data folder (~/.local/share), CSIDL_PERSONAL the home
// folder's documents and CSIDL_DESKTOPDIRECTORY the desktop.
#define CSIDL_PERSONAL         0x0005
#define CSIDL_DESKTOPDIRECTORY 0x0010
#define CSIDL_APPDATA          0x001a

BOOL SHGetSpecialFolderPath(HWND owner, LPWSTR path, int folder, BOOL create);
