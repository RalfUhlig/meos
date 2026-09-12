// Linux port: stand-in for the MSVC header <tchar.h>. MeOS is always built as Unicode.
#pragma once

#include "../msvc_compat.h"

#define _T(s) L##s
#define _TEXT(s) L##s

#define _tcslen   wcslen
#define _tcscmp   wcscmp
#define _tcsicmp  _wcsicmp
#define _tcsnicmp _wcsnicmp
#define _ttoi     _wtoi
