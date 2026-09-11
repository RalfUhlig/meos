// Linux port: stand-in for the MSVC header <tchar.h>. MeOS is always built as Unicode.
#pragma once

#define _T(s) L##s
#define _TEXT(s) L##s
