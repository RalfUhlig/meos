// Linux port: stand-in for the Windows SDK header <objbase.h>.
// MeOS uses COM only for the folder browser and its task allocator. There is no COM on
// Linux; CoInitializeEx does nothing and the interfaces declare just the methods MeOS
// calls. Implemented in code/platform/qt/win32_dialogs.cpp (stage 1.2, not yet
// available).

#pragma once

#include "windows.h"

#define S_OK    ((HRESULT)0)
#define S_FALSE ((HRESULT)1)

#define COINIT_APARTMENTTHREADED 0x2

HRESULT CoInitializeEx(LPVOID reserved, DWORD coInit);

struct IUnknown {
  virtual ULONG AddRef() = 0;
  virtual ULONG Release() = 0;

protected:
  ~IUnknown() = default;
};

struct IMalloc : IUnknown {
  virtual void *Alloc(SIZE_T size) = 0;
  virtual void Free(void *memory) = 0;

protected:
  ~IMalloc() = default;
};

typedef IMalloc *LPMALLOC;
