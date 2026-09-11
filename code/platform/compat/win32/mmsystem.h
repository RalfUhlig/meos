// Linux port: stand-in for the Windows SDK header <mmsystem.h>.
// PlaySound is implemented by the platform sound backend (stage 3, Qt Multimedia).
#pragma once

#include "windows.h"

#define SND_SYNC        0x0000
#define SND_ASYNC       0x0001
#define SND_NODEFAULT   0x0002
#define SND_MEMORY      0x0004
#define SND_LOOP        0x0008
#define SND_NOSTOP      0x0010
#define SND_PURGE       0x0040
#define SND_APPLICATION 0x0080
#define SND_ALIAS       0x00010000L
#define SND_FILENAME    0x00020000L
#define SND_RESOURCE    0x00040004L

BOOL PlaySound(LPCWSTR sound, HMODULE module, DWORD flags);
