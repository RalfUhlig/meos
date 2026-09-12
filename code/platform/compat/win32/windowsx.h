// Linux port: stand-in for the Windows SDK header <windowsx.h>.
// Only the message cracker macros MeOS uses.

#pragma once

#include "windows.h"

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
