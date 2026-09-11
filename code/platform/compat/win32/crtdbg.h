// Linux port: stand-in for the MSVC header <crtdbg.h>.
// The MSVC debug heap does not exist on Linux; debug builds use AddressSanitizer
// for leak detection instead (see code/CMakeLists.txt).
#pragma once

#include <cassert>

#define _CRTDBG_ALLOC_MEM_DF 0x01
#define _CRTDBG_LEAK_CHECK_DF 0x20

#define _CrtSetDbgFlag(flag) (0)
#define _CrtDumpMemoryLeaks() (0)

#define _ASSERT(expr) assert(expr)
#define _ASSERTE(expr) assert(expr)
