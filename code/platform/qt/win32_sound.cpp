/************************************************************************
    MeOS - Orienteering Software
    Linux port: sound output (PlaySound).

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Until stage 3 plays sounds with Qt Multimedia, PlaySound behaves as on a Windows
// computer without a sound device: nothing is played and the call fails. The WAVE
// resources are already embedded (meos_resources). MeOS ignores the result.

#include "windows.h"

#include "mmsystem.h"

BOOL PlaySound(LPCWSTR /*sound*/, HMODULE /*module*/, DWORD /*flags*/) {
  return FALSE;
}
