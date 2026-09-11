/************************************************************************
    MeOS - Orienteering Software
    Linux port: compile-time checks of binary file layouts.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// The runner database (RunnerDB.cpp) is stored as raw structs, and the file format
// version is derived from sizeof. These sizes are the ones of the Windows x64 build;
// a mismatch would make database files incompatible between Windows and Linux.

#include "StdAfx.h"
#include "RunnerDB.h"

static_assert(sizeof(RunnerDBEntryV1) == 48, "RunnerDBEntryV1 differs from the Windows layout");
static_assert(sizeof(RunnerDBEntryV2) == 56, "RunnerDBEntryV2 differs from the Windows layout");
static_assert(sizeof(RunnerDBEntryV3) == 80, "RunnerDBEntryV3 differs from the Windows layout");
static_assert(sizeof(RunnerDBEntry) == 120, "RunnerDBEntry differs from the Windows layout");
